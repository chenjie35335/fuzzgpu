# Build from the repository root:
#   docker build -t fuzzgpu-artifact .
#
# Optional proxy configuration:
#   docker build --add-host=host.docker.internal:host-gateway \
#     --build-arg http_proxy=http://host.docker.internal:7890 \
#     --build-arg https_proxy=http://host.docker.internal:7890 \
#     -t fuzzgpu-artifact .
# (This may be needed if you encounter download errors or missing files)
#
# Run with a large executable tmpfs for experiment work directories:
#   docker run -it --name fuzzgpu --tmpfs /tmp:rw,size=600g,exec fuzzgpu-artifact bash
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG http_proxy
ARG https_proxy

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# set up PROXY (optional):
#   docker build --add-host=host.docker.internal:host-gateway \
#     --build-arg http_proxy=http://host.docker.internal:7890 \
#     --build-arg https_proxy=http://host.docker.internal:7890 .
ENV http_proxy=${http_proxy}
ENV https_proxy=${https_proxy}

# goto home
WORKDIR /root

RUN apt-get update && apt-get install -y curl tmux ca-certificates

# Install oh my zsh and some convenience plugins.
RUN apt-get install -y zsh git && \
    RUNZSH=no CHSH=no KEEP_ZSHRC=yes \
      sh -c "$(curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh)" && \
    git clone https://github.com/zsh-users/zsh-autosuggestions \
      "${ZSH_CUSTOM:-/root/.oh-my-zsh/custom}/plugins/zsh-autosuggestions" && \
    git clone https://github.com/zsh-users/zsh-syntax-highlighting \
      "${ZSH_CUSTOM:-/root/.oh-my-zsh/custom}/plugins/zsh-syntax-highlighting" && \
    sed -i 's/plugins=(git)/plugins=(git zsh-autosuggestions zsh-syntax-highlighting)/' /root/.zshrc

ENV SHELL=/bin/zsh

RUN apt-get update && apt-get install -y git

# install deps for vortex
RUN apt-get install -y libreadline-dev uuid-dev pkg-config doxygen graphviz gcc g++ make cmake vim
RUN apt-get install -y wget bzip2

RUN apt-get install -y \
    mold ccache ninja-build cmake clang clangd clang-format gdb \
    help2man perl perl-doc flex bison libfl2 libfl-dev zlib1g zlib1g-dev libgoogle-perftools-dev numactl \
    libfmt-dev libspdlog-dev libelf-dev libyaml-cpp-dev nlohmann-json3-dev \
    device-tree-compiler bsdmainutils ruby default-jdk python3-tqdm

# install deps for verilator
RUN apt-get install -y build-essential
RUN apt-get install -y \
  git help2man perl python3 make autoconf g++ flex bison ccache \
  libfl2 libfl-dev zlib1g zlib1g-dev \
  libgoogle-perftools-dev libjemalloc-dev numactl perl-doc

# firrtl
RUN wget -O firrtl-bin-linux-x64.tar.gz \
  https://github.com/llvm/circt/releases/download/firtool-1.62.0/firrtl-bin-linux-x64.tar.gz && \
  mkdir -p /root/firtool-1.62.0 && \
  tar -xzf firrtl-bin-linux-x64.tar.gz \
    -C /root/firtool-1.62.0 \
    --strip-components=1 && \
  rm -f firrtl-bin-linux-x64.tar.gz

# pyenv
ENV PYENV_ROOT=/root/.pyenv
ENV PATH=${PYENV_ROOT}/bin:${PYENV_ROOT}/shims:/root/firtool-1.62.0/bin:${PATH}

RUN apt-get install -y \
  make build-essential libssl-dev zlib1g-dev \
  libbz2-dev libreadline-dev libsqlite3-dev curl git \
  libncursesw5-dev xz-utils tk-dev libxml2-dev \
  libxmlsec1-dev libffi-dev liblzma-dev

RUN curl https://pyenv.run | bash

RUN echo 'eval "$(pyenv init --path)"' >> /root/.zshrc && \
    echo 'eval "$(pyenv init -)"' >> /root/.zshrc

RUN mkdir -p /root/tmp && \
    eval "$(pyenv init --path)" && \
    eval "$(pyenv init -)" && \
    TMPDIR=/root/tmp PYTHON_CONFIGURE_OPTS="--enable-shared" pyenv install 3.10.12 && \
    pyenv global 3.10.12

RUN apt-get install -y python3-virtualenv && \
    eval "$(pyenv init --path)" && \
    eval "$(pyenv init -)" && \
    virtualenv -p python3.10 /root/envs && \
    /root/envs/bin/python -m pip install --no-cache-dir --upgrade pip setuptools wheel

# Install makeelf for cascade
RUN git clone https://github.com/flaviens/makeelf && cd makeelf && git checkout finercontrol && /root/envs/bin/python -m pip install --no-cache-dir .

# install deps for DiveFuzz
RUN apt install -y libboost-regex-dev libboost-system-dev

################ copy source code
COPY . /root/fuzzgpu

WORKDIR /root/fuzzgpu

# cascade meta
COPY baseline/cascade-ventus /cascade-ventus
COPY baseline/cascade-vortex32 /cascade-vortex32
COPY baseline/cascade-vortex64 /cascade-vortex64

# checkout Verilator
RUN git submodule update --init --depth 1 --recursive thirdparty/verilator

# python packages for Cascade & DiveFuzz & FuzzGPU scripts
RUN source /root/envs/bin/activate && \
    pip install --no-cache-dir -r requirements.txt

# Generate the riscv-opcodes files used by DiveFuzz's hybrid encoder.
# These files are generated artifacts and are intentionally not tracked.
RUN source /root/envs/bin/activate && \
    make -C baseline/DiveFuzz/fuzzer/generator/reg_analyzer/riscv-opcodes

# build Spike for Cascade
RUN cd baseline/cascade-meta/tools/riscv-isa-sim && \
    mkdir -p build && \
    cd build && \
    ../configure --prefix=/root/prefix-cascade/riscv && \
    make -j"$(nproc)" && \
    make install

# build ventus-env toolchain and env
RUN apt install -y libtool
RUN cd targets/ventus-env && \
    wget -O rodinia_data.tar.xz http://dspdev.ime.tsinghua.edu.cn/images/ventus_dataset/ventus_rodinia_data.tar.xz && \
    tar -xf rodinia_data.tar.xz && \
    bash build-ventus.sh

# build all (ensure network connecting, it will download toolchain)
RUN source /root/envs/bin/activate && \
    ./build.sh --all

# DiveFuzz ref engines
RUN source /root/envs/bin/activate && \
    cd baseline/DiveFuzz/ref/gpu-isa-emulator && \
    ./build_all.sh



CMD ["/bin/zsh"]

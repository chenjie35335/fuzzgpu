# Quick Start


### Install Vortex-dffitest codebase
```sh
	git clone --depth=1 --recursive -b difftest git@github.com:gpu-experimental/vortex-difftest.git
	cd vortex-difftest
```
### Install system dependencies
```sh
# ensure dependent libraries are present
sudo ./ci/install_dependencies.sh
```
### Configure your build folder
```sh
    mkdir build
    cd build
    # for 32bit
    ../configure --xlen=32 --tooldir=$HOME/tools
    # for 64bit
    ../configure --xlen=64 --tooldir=$HOME/tools
```
### Install prebuilt toolchain
```sh
   ./ci/toolchain_install.sh --all
```
### set environment variables
```sh
    # should always run before using the toolchain!
    source ./ci/toolchain_env.sh
```
### Building Vortex
```sh
make -s
```

### Build difftest-gpu

Requirements:
```bash
    sudo apt install spdlog-dev
```

```bash
    git clone --recursive git@github.com:gpu-experimental/difftest-gpu.git
```


### Quick demo running sgemm
```sh
    export DIFFTEST_HOME="path/to/the/difftest_gpu"
    ../configure --xlen=32 --tooldir=$HOME/tools && ./ci/blackbox.sh --driver=difftest --app=sgemm
```

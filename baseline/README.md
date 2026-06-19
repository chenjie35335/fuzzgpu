# Creation
PYTHON_CONFIGURE_OPTS="--enable-shared" pyenv install 3.10.12
cd /home/ain/fuzz_gpgpu/fuzzgpu/baseline/cascade-meta
pyenv local 3.10.12
/home/ain/.pyenv/versions/3.10.12/lib/libpython3.10.so

mkdir -p "$HOME/envs"
virtualenv -p python3.10 "$HOME/envs"

# Usage

which python  
$HOME/envs/bin/python

cd /home/ain/fuzz_gpgpu/fuzzgpu/baseline/cascade-meta
source "$HOME/envs/bin/activate"

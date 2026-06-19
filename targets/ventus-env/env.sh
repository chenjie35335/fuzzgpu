if [ -n "${BASH_SOURCE[0]:-}" ]; then
	_env_script_path="${BASH_SOURCE[0]}"
elif [ -n "${ZSH_VERSION:-}" ]; then
	_env_script_path="${(%):-%N}"
else
	_env_script_path="$0"
fi

DIR=$(cd "$(dirname "$_env_script_path")" && pwd)
unset _env_script_path
export VENTUS_INSTALL_PREFIX=${DIR}/install
export PATH=${VENTUS_INSTALL_PREFIX}/bin:$PATH
export LD_LIBRARY_PATH=${VENTUS_INSTALL_PREFIX}/lib:${LD_LIBRARY_PATH:-}
export POCL_DEVICES="ventus"
export OCL_ICD_VENDORS=${VENTUS_INSTALL_PREFIX}/lib/libpocl.so

# see https://pcn2po10nqam.feishu.cn/wiki/XHNXwIdRkiFtZDkCW6Mc58Uon3b
export POCL_ENABLE_UNINIT=1

# remove extra colons
export LD_LIBRARY_PATH=$(echo "$LD_LIBRARY_PATH" | sed -e 's/^:*//' -e 's/:*$//')


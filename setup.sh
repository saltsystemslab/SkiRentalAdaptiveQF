#!/usr/bin/env bash
# Requires gcc/g++, cmake, git, curl, OpenSSL/libaio headers and Python venv.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p external
JOBS="${JOBS:-4}"
if [[ ! -d external/wiredtiger ]]; then
    curl -fL https://github.com/wiredtiger/wiredtiger/archive/refs/tags/11.3.1.tar.gz \
        -o external/wiredtiger-11.3.1.tar.gz
    tar -xf external/wiredtiger-11.3.1.tar.gz -C external
    mv external/wiredtiger-11.3.1 external/wiredtiger
fi
# CMake caches embed absolute paths, including paths from a moved checkout.
WT_SOURCE="$(cd external/wiredtiger && pwd -P)"
WT_CACHE=external/wiredtiger/build/CMakeCache.txt
if [[ -f "$WT_CACHE" ]] && [[ "$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$WT_CACHE")" != "$WT_SOURCE" ]]; then
    rm -f "$WT_CACHE"
    rm -rf external/wiredtiger/build/CMakeFiles
fi
cmake -S external/wiredtiger -B external/wiredtiger/build \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-Wno-array-bounds
cmake --build external/wiredtiger/build --target wiredtiger_shared -j "$JOBS"
checkout_dependency() {
    local name="$1" url="$2" revision="$3"
    if [[ ! -d "external/$name" ]]; then
        git clone "$url" "external/$name"
    fi
    git -C "external/$name" checkout "$revision"
}
checkout_dependency splinterdb https://github.com/vmware/splinterdb.git 05654abb
COMPILER=gcc CC=gcc LD=gcc make -C external/splinterdb -j "$JOBS"
checkout_dependency cxxopts https://github.com/jarro2783/cxxopts.git \
    60286ded02a170c85836bc73be1d021168f0f7b4
checkout_dependency cpp_random_distributions https://github.com/llersch/cpp_random_distributions.git \
    6ae38ecf5dbeb1150b80ecf051cb43cb88ba697a
python3 -m venv .venv
# Sacred 0.8.7 imports pkg_resources, which a bare Python 3.12 venv lacks.
.venv/bin/python -m pip install 'setuptools==68.1.2' 'sacred==0.8.7' pandas tqdm
echo 'Setup complete. Use .venv/bin/python experiments/run.py --list'

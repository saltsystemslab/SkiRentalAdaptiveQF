#!/bin/bash

mkdir -p external

cd external

# BUILD WiredTiger
wget https://github.com/wiredtiger/wiredtiger/archive/refs/tags/11.3.1.tar.gz 
tar -xvf 11.3.1.tar.gz
mv wiredtiger-11.3.1/ wiredtiger
cd wiredtiger
sed -i '/^project(WiredTiger/a add_compile_options(-Wno-array-bounds)' CMakeLists.txt
mkdir build
cd build
cmake ../ -DCMAKE_BUILD_TYPE=Release
make wiredtiger_shared
cd ../../ # Back to external

# BUILD SplinterDB
git clone git@github.com:vmware/splinterdb
cd splinterdb
git checkout 05654ab
export COMPILER=gcc
export CC=$COMPILER
export LD=$COMPILER
make
cd ../ # Back to external

git clone git@github.com:jarro2783/cxxopts
git clone git@github.com:llersch/cpp_random_distributions

pip3 install sacred pandas tqdm --break-system-packages

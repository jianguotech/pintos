make clean
make
cd build
make clean
make check SIMULATOR=--qemu > output 2>&1

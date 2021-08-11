#!/bin/bash
#set -x 

PROJECT=achilles6_row_wifi
CONFIG=${PROJECT}_defconfig
GCC_PATH=${PWD}/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin
CC=${PWD}/prebuilts/clang/host/linux-x86/clang-4691093/bin/clang

export PATH=$GCC_PATH:$PATH

make -j24 -C kernel-4.9 O=../out/target/product/${PROJECT}/obj/KERNEL_OBJ ARCH=arm64 CROSS_COMPILE=aarch64-linux-android- CLANG_TRIPLE=aarch64-linux-gnu- CC=${CC} ${CONFIG}
cp kernel-4.9/include out/target/product/${PROJECT}/obj/KERNEL_OBJ/ -rf
make -j24 V=1 -C kernel-4.9 O=../out/target/product/${PROJECT}/obj/KERNEL_OBJ ARCH=arm64 CROSS_COMPILE=aarch64-linux-android- CLANG_TRIPLE=aarch64-linux-gnu- CC=${CC}
make -j24 -C kernel-4.9 O=../out/target/product/${PROJECT}/obj/KERNEL_OBJ ARCH=arm64 CROSS_COMPILE=aarch64-linux-android- CLANG_TRIPLE=aarch64-linux-gnu- CC=${CC} modules
make -j24 -C kernel-4.9 O=../out/target/product/${PROJECT}/obj/KERNEL_OBJ ARCH=arm64 CROSS_COMPILE=aarch64-linux-android- CLANG_TRIPLE=aarch64-linux-gnu- CC=${CC} modules_install INSTALL_MOD_PATH=../../vendor

#You can get kernel img:
#out/target/product/achilles6_row_wifi/obj/KERNEL_OBJ/arch/arm64/boot/Image.gz-dtb

#!/bin/bash

if [ ! -z $1 ]
then
	PROJECT=Rockchip DEVICE=RK3588 ARCH=aarch64 ./scripts/clean $1
	PROJECT=Amlogic DEVICE=S922X ARCH=aarch64 ./scripts/clean $1
	DEVICE_ROOT=RK3566 PROJECT=Rockchip DEVICE=RK3566 ARCH=aarch64 ./scripts/clean $1
	PROJECT=Rockchip DEVICE=RK3326 ARCH=aarch64 ./scripts/clean $1
	PROJECT=Rockchip DEVICE=RK3399 ARCH=aarch64 ./scripts/clean $1
	PROJECT=Allwinner DEVICE=H700 ARCH=aarch64 ./scripts/clean $1
	PROJECT=Qualcomm DEVICE=SM8250 ARCH=aarch64 ./scripts/clean $1
	PROJECT=Qualcomm DEVICE=SM8550 ARCH=aarch64 ./scripts/clean $1
        PROJECT=Qualcomm DEVICE=SDM845 ARCH=aarch64 ./scripts/clean $1
fi

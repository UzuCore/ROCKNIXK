#!/bin/bash

if [ ! -z $1 ]
then
	PROJECT=ROCKNIX DEVICE=RK3588 ARCH=aarch64 ./scripts/clean $1;
	PROJECT=ROCKNIX DEVICE=S922X ARCH=aarch64 ./scripts/clean $1;
	PROJECT=ROCKNIX DEVICE=RK3566 ARCH=aarch64 ./scripts/clean $1;
	PROJECT=ROCKNIX DEVICE=RK3326 ARCH=aarch64 ./scripts/clean $1;
	PROJECT=ROCKNIX DEVICE=RK3399 ARCH=aarch64 ./scripts/clean $1;
	PROJECT=ROCKNIX DEVICE=H700 ARCH=aarch64 ./scripts/clean $1;
	PROJECT=ROCKNIX DEVICE=SM8250 ARCH=aarch64 ./scripts/clean $1;
	PROJECT=ROCKNIX DEVICE=SM8550 ARCH=aarch64 ./scripts/clean $1;
	PROJECT=ROCKNIX DEVICE=SM8650 ARCH=aarch64 ./scripts/clean $1;
        PROJECT=ROCKNIX DEVICE=SDM845 ARCH=aarch64 ./scripts/clean $1;
fi

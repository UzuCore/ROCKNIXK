#!/bin/bash

TARGET="RK3588 RK3566 RK3326 RK3399 S922X SM8250 SM8550 SDM845 H700"

for var in $TARGET
do make $var;
done

#./.clean.sh same_cdi-lr
./.build.sh same_cdi-lr

#./.clean.sh mame-lr
./.build.sh mame-lr

for var in $TARGET
do make $var;
done

# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2019-present Shanti Gilbert (https://github.com/shantigilbert)
# Maintenance 2020 351ELEC team (https://github.com/fewtarius/351ELEC)
# Copyright (C) 2023 JELOS (https://github.com/JustEnoughLinuxOS)

PKG_NAME="fbneo-xtreme-lr"
PKG_VERSION="a3ad2d78194271e1d84a9676425bfb0e17c7b379"
PKG_LICENSE="Non-commercial"
PKG_SITE="https://github.com/KMFDManic/FBNeo-Xtreme-Amped"
PKG_URL="${PKG_SITE}/archive/${PKG_VERSION}.tar.gz"
PKG_DEPENDS_TARGET="toolchain"
PKG_LONGDESC="FinalBurn Neo Xtreme-Amped"
PKG_TOOLCHAIN="make"


pre_configure_target() {
sed -i "s|LDFLAGS += -static-libgcc -static-libstdc++|LDFLAGS += -static-libgcc|"  ./src/burner/libretro/Makefile

PKG_MAKE_OPTS_TARGET=" -C ./src/burner/libretro USE_CYCLONE=0 profile=performance"

if [[ "${TARGET_FPU}" =~ "neon" ]]; then
	PKG_MAKE_OPTS_TARGET+=" HAVE_NEON=1"
fi

}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/lib/libretro
  cp ${PKG_DIR}/fbneo-xtreme_libretro.info ${INSTALL}/usr/lib/libretro/
  cp ${PKG_BUILD}/src/burner/libretro/km_fbneo_xtreme_amped_libretro.so ${INSTALL}/usr/lib/libretro/km_fbneo_xtreme_amped_libretro.so
}

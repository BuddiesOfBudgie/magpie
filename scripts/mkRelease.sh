#!/bin/bash
set -e

git submodule init
git submodule update

rm -rf build
meson setup build --prefix=/usr
ninja dist -C build

VERSION=$(grep "version:" meson.build | head -n1 | cut -d"'" -f2)
TAR="magpie-${VERSION}.tar.xz"
VTAR="magpie-v${VERSION}.tar.xz"

mv build/meson-dist/$TAR $VTAR

gpg --armor --detach-sign $VTAR
gpg --verify "${VTAR}.asc"

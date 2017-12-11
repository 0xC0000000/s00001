#!/bin/sh

TOPDIR=`pwd`

# multi compile jobs feature, e.g. [./make_ap143.sh -j 8]
jobs=1
if [ $# -ge 1 ]; then
    OPTION=$1
    if [ "$OPTION" = "-j" ]; then
        case "$2" in
            '' | *[!0-9]*)
                echo "invalid jobs number" >&2; exit 1;;
        esac

        if [ "$2" -gt "0" ]; then
            jobs=$2
        fi
    fi
fi

if [ ! -d "feeds" ]; then
    ./scripts/feeds update -a
fi

rm -rf tmp
rm -rf build_dir/target-*/hos-*
rm -rf build_dir/target-*/linux-ar71xx_generic/base-files

rm -rf .config
cp ap143.config .config
make defconfig

if [ $jobs -gt 1 ]; then
    echo "=== $jobs jobs compiling ==="
    make -j$jobs V=s
else
    make V=s
fi

if [ $? -ne 0 ]; then
	echo '[Error]: Compile failed.'
	exit 1
fi


cd bin/ar71xx/
mv openwrt-ar71xx-generic-ap143-kernel.bin ap143-kernel.bin
mv openwrt-ar71xx-generic-ap143-rootfs-squashfs.bin ap143-rootfs.bin
mv openwrt-ar71xx-generic-ap143-squashfs-sysupgrade.bin ap143-sysupgrade.bin
cd ../..

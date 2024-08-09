#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
TOOLCHAIN_PATH=$(dirname $(which ${CROSS_COMPILE}gcc))
MESSAGE=

function headerMessage()
{
    echo -------------------------------------
    echo
    echo ${MESSAGE}
    echo
    echo -------------------------------------
}

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

if [ ! -d ${OUTDIR} ]; then
    ret=`mkdir -p ${OUTDIR}`
    if [ ! $ret = "0" ]; then
        echo could not create ${OUTPUT}
        exit 1
    fi
fi

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # 1TODO: Add your kernel build steps here
    MESSAGE="Deep clean kernel source"
    headerMessage
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE mrproper
    sed -i 's/YYLTYPE yylloc;/extern YYLTYPE yylloc;/' scripts/dtc/dtc-lexer.l

    MESSAGE="Run make defconfig"
    headerMessage
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE defconfig

    MESSAGE="Compiling kernel..."
    headerMessage
    make -j4 ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE all

    MESSAGE="Compiling modules..."
    headerMessage
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE modules

    MESSAGE="Compiling device tree..."
    headerMessage
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE dtbs
fi

echo "Adding the Image in outdir"

cd "$OUTDIR"
cp linux-stable/arch/$ARCH/boot/Image $OUTDIR/
cp linux-stable/arch/$ARCH/boot/Image.gz $OUTDIR/

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm -rf ${OUTDIR}/rootfs
fi

# 1TODO: Create necessary base directories
mkdir -p "${OUTDIR}/rootfs"
cd ${OUTDIR}/rootfs
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
else
    cd busybox
fi

# 1TODO: Make and install busybox
MESSAGE="Busybox distclean..."
headerMessage
make clean
make distclean
MESSAGE="Busybox defconfig..."
headerMessage
make defconfig
MESSAGE="Busybox ARCH & CROSS_COMPILE set..."
headerMessage
echo make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
MESSAGE="Busybox install..."
headerMessage
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

# 1TODO: Add library dependencies to rootfs
echo "Library dependencies"
cd ${OUTDIR}/rootfs
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"
lib_dest64=${OUTDIR}/rootfs/lib64
cp $(find ${TOOLCHAIN_PATH}/../ | grep ld-linux-aarch64.so.1) ${OUTDIR}/rootfs/lib
cp $(find ${TOOLCHAIN_PATH}/../ | grep libm.so.6) ${lib_dest64}
cp $(find ${TOOLCHAIN_PATH}/../ | grep libresolv.so.2) ${lib_dest64}
cp $(find ${TOOLCHAIN_PATH}/../ | grep libc.so.6) ${lib_dest64}

# TODO: Make device nodes
if [ ! -e dev/null ]; then
    sudo mknod -m 666 dev/null c 1 3
    sudo mknod -m 666 dev/mem c 1 1
    sudo mknod -m 666 dev/ram0 b 1 1
fi
if [ ! -e dev/ttyS0 ]; then
    sudo mknod -m 666 dev/console c 5 1
fi

# 1TODO: Clean and build the writer utility
cd $FINDER_APP_DIR
MESSAGE="Creating root filesystem..."
headerMessage
make clean
make all

mkdir -p ${OUTDIR}/rootfs/home/conf
cp ./writer ${OUTDIR}/rootfs/home
cp ./finder.sh ${OUTDIR}/rootfs/home
cp ./conf/username.txt ${OUTDIR}/rootfs/home/conf/
cp ./conf/assignment.txt ${OUTDIR}/rootfs/home/conf/
cp ./finder-test.sh ${OUTDIR}/rootfs/home
cp ./autorun-qemu.sh ${OUTDIR}/rootfs/home
cd ${OUTDIR}/rootfs/
echo root:x:0:0:root:/root:/bin/sh > ${OUTDIR}/rootfs/etc/passwd
sudo chown -R root:root *

echo Create RAM disk image. Archive and compress rootfs
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
cd ${OUTDIR}
gzip -f initramfs.cpio

#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

CURRENT_DIR="$(pwd)"

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

    # TODO: Add your kernel build steps here
    
    # deep clean build tree
    make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- mrproper

    #configure "virt" arm device to simulate with QEMU
    make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- defconfig

    #build actual kernel for booting with QEMU
    make -j4 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- all

   #build modules
   make -j8 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- modules

   #build devicetree
   make -j8 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- dtbs
    
fi


echo "Adding the Image in outdir"
cp "$OUTDIR"/linux-stable/arch/arm64/boot/Image ${OUTDIR}

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
    echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories

# create empty Linux fs tree
mkdir -p rootfs
ROOTFS=${OUTDIR}/rootfs
cd "$ROOTFS"
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
    # I used busybox mirror because original busybox may have connection problems
    git clone https://github.com/mirror/busybox.git
    #git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    make distclean
    make defconfig
    # TODO:  Configure busybox
else
    cd busybox
fi

# TODO: Make and install busybox
make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install


echo "Library dependencies"
${CROSS_COMPILE}readelf -a  ${ROOTFS}/bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a  ${ROOTFS}/bin/busybox | grep "Shared library"


# TODO: Add library dependencies to rootfs

files=$(${CROSS_COMPILE}readelf -d ${ROOTFS}/bin/busybox | grep NEEDED | sed 's/\[//g; s/\]//g;' | awk '{print $NF}')
interpreter=$(${CROSS_COMPILE}readelf -a ${ROOTFS}/bin/busybox | grep "program interpreter" | sed 's/\[//g; s/\]//g;' | awk '{print $NF}')
echo $interpreter

sysroot=$(${CROSS_COMPILE}gcc -print-sysroot)

for i in $files; do
    cp -fv $sysroot/lib64/$i ${ROOTFS}/lib64
done
cp -fv $sysroot/$interpreter ${ROOTFS}/lib


# TODO: Make device nodes

sudo  rm -f  ${ROOTFS}/dev/null ${ROOTFS}/dev/console
sudo mknod -m 666  ${ROOTFS}/dev/null    c 1 3
sudo mknod -m 666  ${ROOTFS}/dev/console c 5 1

# TODO: Clean and build the writer utility

cd "${CURRENT_DIR}"
make clean && CROSS_COMPILE="aarch64-none-linux-gnu-" make

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cd "${CURRENT_DIR}"
cp writer autorun-qemu.sh finder.sh  finder-test.sh  ${ROOTFS}/home
mkdir -p  ${ROOTFS}/conf
cp  conf/username.txt conf/assignment.txt ${ROOTFS}/conf
cp -r ${ROOTFS}/conf ${ROOTFS}/home

# TODO: Chown the root directory

# TODO: Create initramfs.cpio.gz

cd ${ROOTFS}
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
gzip -f ${OUTDIR}/initramfs.cpio

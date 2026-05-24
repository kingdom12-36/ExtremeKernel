#!/bin/bash

abort()
{
    cd -
    echo "-----------------------------------------------"
    echo "Kernel compilation failed! Exiting..."
    echo "-----------------------------------------------"
    exit -1
}

unset_flags()
{
    cat << EOF
Usage: $(basename "$0") [options]
Options:
    -m, --model [value]     Specify the model code of the phone
    -k, --ksu [Y/n]         Include KernelSU
    -r, --recovery [y/N]    Compile kernel for an Android Recovery
    -n, --monitor [Y/n]     Enable WiFi monitor mode (BCM4375 WL_MONITOR + monitor.config)
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --model|-m)
            MODEL="$2"
            shift 2
            ;;
        --ksu|-k)
            KSU_OPTION="$2"
            shift 2
            ;;
        --recovery|-r)
            RECOVERY_OPTION="$2"
            shift 2
            ;;
        --monitor|-n)
            MONITOR_OPTION="$2"
            shift 2
            ;;
        *)\
            unset_flags
            exit 1
            ;;
    esac
done

echo "Preparing the build environment..."

pushd $(dirname "$0") > /dev/null
CORES=`cat /proc/cpuinfo | grep -c processor`

# Define toolchain variables
CLANG_DIR=$PWD/toolchain/neutron_18
PATH=$CLANG_DIR/bin:$PATH

# Check if toolchain exists
if [ ! -f "$CLANG_DIR/bin/clang-18" ]; then
    echo "-----------------------------------------------"
    echo "Toolchain not found! Downloading..."
    echo "-----------------------------------------------"
    rm -rf $CLANG_DIR
    mkdir -p $CLANG_DIR
    pushd toolchain/neutron_18 > /dev/null
    bash <(curl -s "https://raw.githubusercontent.com/Neutron-Toolchains/antman/main/antman") -S=05012024
    echo "-----------------------------------------------"
    echo "Patching toolchain..."
    echo "-----------------------------------------------"
    bash <(curl -s "https://raw.githubusercontent.com/Neutron-Toolchains/antman/main/antman") --patch=glibc
    echo "-----------------------------------------------"
    echo "Cleaning up..."
    popd > /dev/null
fi

MAKE_ARGS="
LLVM=1 \
LLVM_IAS=1 \
ARCH=arm64 \
O=out
"

# Define specific variables
case $MODEL in
beyond0lte)
    BOARD=SRPRI28A016KU
    SOC=exynos9820
;;
beyond1lte)
    BOARD=SRPRI28B016KU
    SOC=exynos9820
;;
beyond2lte)
    BOARD=SRPRI17C016KU
    SOC=exynos9820
;;
beyondx)
    BOARD=SRPSC04B014KU
    SOC=exynos9820
;;
d1)
    BOARD=SRPSD26B009KU
    SOC=exynos9825
;;
d1xks)
    BOARD=SRPSD23A002KU
    SOC=exynos9825
;;
d2s)
    BOARD=SRPSC14B009KU
    SOC=exynos9825
;;
d2x)
    BOARD=SRPSC14C009KU
    SOC=exynos9825
;;
d2xks)
    BOARD=SRPSD23C002KU
    SOC=exynos9825
;;
*)
    unset_flags
    exit
esac

if [[ "$RECOVERY_OPTION" == "y" ]]; then
    RECOVERY=recovery.config
    KSU_OPTION=n
fi

if [ -z $KSU_OPTION ]; then
    read -p "Include KernelSU (y/N): " KSU_OPTION
fi

if [[ "$KSU_OPTION" == "y" || "$KSU_OPTION" == "Y" ]]; then
    KSU=ksu.config
fi

# Monitor mode: output goes to a separate directory so it never clobbers
# the standard build output (e.g. d2s-monitor instead of d2s)
MODEL_OUT=$MODEL
if [[ "$MONITOR_OPTION" == "Y" || "$MONITOR_OPTION" == "y" ]]; then
    MONITOR=monitor.config
    MODEL_OUT="${MODEL}-monitor"
fi

rm -rf build/out/$MODEL_OUT
mkdir -p build/out/$MODEL_OUT/zip/files
mkdir -p build/out/$MODEL_OUT/zip/META-INF/com/google/android

# Apply susfs kernel patches (required for KernelSU susfs support)
echo "-----------------------------------------------"
echo "Applying susfs kernel patches..."
echo "-----------------------------------------------"
SUSFS_PATCH="build/patches/50_add_susfs_in_kernel-4.14.patch"
if [ -f "$SUSFS_PATCH" ]; then
    patch -p1 --forward --batch < "$SUSFS_PATCH" && echo "susfs patches applied." || echo "susfs patches already applied or skipped."
else
    echo "susfs patch not found at $SUSFS_PATCH, skipping."
fi
echo "-----------------------------------------------"

# Enable WL_MONITOR in the BCM4375 DHD driver when monitor mode is requested.
# This adds -DWL_MONITOR to the driver's CFLAGS so the kernel exposes a
# mon0 monitor interface that iw / tcpdump can use.
if [ -n "$MONITOR" ]; then
    echo "-----------------------------------------------"
    echo "Monitor mode requested — enabling WL_MONITOR in BCM DHD driver..."
    echo "-----------------------------------------------"
    DHD_MAKEFILE=$(grep -rl "bcmdhd\|dhd_linux\|bcm4375\|DHD" drivers/net/wireless/ 2>/dev/null \
                   | grep "Makefile" | head -1)
    if [ -n "$DHD_MAKEFILE" ]; then
        if ! grep -q "WL_MONITOR" "$DHD_MAKEFILE"; then
            echo "" >> "$DHD_MAKEFILE"
            echo "# ExtremeKernel: WiFi monitor mode (attack detection)" >> "$DHD_MAKEFILE"
            echo "DHDCFLAGS += -DWL_MONITOR" >> "$DHD_MAKEFILE"
            echo "WL_MONITOR injected into $DHD_MAKEFILE"
        else
            echo "WL_MONITOR already present in $DHD_MAKEFILE"
        fi
    else
        echo "WARNING: BCM DHD Makefile not found by grep."
        echo "Falling back to EXTRA_CFLAGS (may not reach the DHD driver)."
        MAKE_ARGS="$MAKE_ARGS EXTRA_CFLAGS=-DWL_MONITOR"
    fi
fi

# Build kernel image
echo "-----------------------------------------------"
echo "Defconfig: "$KERNEL_DEFCONFIG""

if [ -z "$KSU" ]; then
    echo "KSU: No"
else
    echo "KSU: Yes"
fi

if [ -z "$RECOVERY" ]; then
    echo "Recovery: N"
else
    echo "Recovery: Y"
fi

if [ -z "$MONITOR" ]; then
    echo "Monitor Mode: No"
else
    echo "Monitor Mode: Yes (WL_MONITOR + monitor.config)"
fi

echo "Output directory: build/out/$MODEL_OUT"
echo "-----------------------------------------------"
echo "Building kernel using "$KERNEL_DEFCONFIG""
echo "Generating configuration file..."
echo "-----------------------------------------------"
make ${MAKE_ARGS} -j$CORES exynos9820_defconfig $MODEL.config $KSU $RECOVERY $MONITOR || abort

echo "Building kernel..."
echo "-----------------------------------------------"
make ${MAKE_ARGS} -j$CORES || abort

# Define constant variables
KERNEL_PATH=build/out/$MODEL_OUT/Image
KERNEL_OFFSET=0x00008000
RAMDISK_OFFSET=0xF0000000
SECOND_OFFSET=0xF0000000
TAGS_OFFSET=0x00000100
BASE=0x10000000
CMDLINE='loop.max_part=7'
HASHTYPE=sha1
HEADER_VERSION=1
OS_PATCH_LEVEL=2025-01
OS_VERSION=14.0.0
PAGESIZE=2048
RAMDISK=build/out/$MODEL_OUT/ramdisk.cpio.gz
OUTPUT_FILE=build/out/$MODEL_OUT/boot.img

## Build auxiliary boot.img files
# Copy kernel to build
cp out/arch/arm64/boot/Image build/out/$MODEL_OUT

echo "-----------------------------------------------"
# Build dtb
if [[ "$SOC" == "exynos9820" ]]; then
    echo "Building common exynos9820 Device Tree Blob Image..."
    echo "-----------------------------------------------"
    ./toolchain/mkdtimg cfg_create build/out/$MODEL_OUT/dtb.img build/dtconfigs/exynos9820.cfg -d out/arch/arm64/boot/dts/exynos
fi

if [[ "$SOC" == "exynos9825" ]]; then
    echo "Building common exynos9825 Device Tree Blob Image..."
    echo "-----------------------------------------------"
    ./toolchain/mkdtimg cfg_create build/out/$MODEL_OUT/dtb.img build/dtconfigs/exynos9825.cfg -d out/arch/arm64/boot/dts/exynos
fi
echo "-----------------------------------------------"

# Build dtbo
echo "Building Device Tree Blob Output Image for "$MODEL"..."
echo "-----------------------------------------------"
./toolchain/mkdtimg cfg_create build/out/$MODEL_OUT/dtbo.img build/dtconfigs/$MODEL.cfg -d out/arch/arm64/boot/dts/samsung
echo "-----------------------------------------------"

if [ -z "$RECOVERY" ]; then
    # Build ramdisk
    echo "Building RAMDisk..."
    echo "-----------------------------------------------"
    pushd build/ramdisk > /dev/null
    find . ! -name . | LC_ALL=C sort | cpio -o -H newc -R root:root | gzip > ../out/$MODEL_OUT/ramdisk.cpio.gz || abort
    popd > /dev/null
    echo "-----------------------------------------------"

    # Create boot image
    echo "Creating boot image..."
    echo "-----------------------------------------------"
    ./toolchain/mkbootimg --base $BASE --board $BOARD --cmdline "$CMDLINE" --hashtype $HASHTYPE \
    --header_version $HEADER_VERSION --kernel $KERNEL_PATH --kernel_offset $KERNEL_OFFSET \
    --os_patch_level $OS_PATCH_LEVEL --os_version $OS_VERSION --pagesize $PAGESIZE \
    --ramdisk $RAMDISK --ramdisk_offset $RAMDISK_OFFSET --second_offset $SECOND_OFFSET \
    --tags_offset $TAGS_OFFSET -o $OUTPUT_FILE || abort

    # Build zip
    echo "Building zip..."
    echo "-----------------------------------------------"
    cp build/out/$MODEL_OUT/boot.img build/out/$MODEL_OUT/zip/files/boot.img
    cp build/out/$MODEL_OUT/dtb.img build/out/$MODEL_OUT/zip/files/dtb.img
    cp build/out/$MODEL_OUT/dtbo.img build/out/$MODEL_OUT/zip/files/dtbo.img

    # Use monitor-specific updater-script if this is a monitor build
    if [ -n "$MONITOR" ] && [ -f "build/updater-script-monitor" ]; then
        cp build/update-binary build/out/$MODEL_OUT/zip/META-INF/com/google/android/update-binary
        cp build/updater-script-monitor build/out/$MODEL_OUT/zip/META-INF/com/google/android/updater-script
        # Include the wifi monitor script so the updater can install it
        if [ -f "build/service.d/wifi_monitor_attacks.sh" ]; then
            cp build/service.d/wifi_monitor_attacks.sh build/out/$MODEL_OUT/zip/files/wifi_monitor_attacks.sh
        fi
    else
        cp build/update-binary build/out/$MODEL_OUT/zip/META-INF/com/google/android/update-binary
        cp build/updater-script build/out/$MODEL_OUT/zip/META-INF/com/google/android/updater-script
    fi

    pushd build/out/$MODEL_OUT/zip > /dev/null

    zip -r ../ExtremeKRNL-Nexus-"$MODEL_OUT".zip .
    popd > /dev/null
fi

popd > /dev/null
echo "Build finished successfully!"

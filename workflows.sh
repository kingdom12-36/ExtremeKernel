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
    -m, --model [value]              Specify the model code of the phone
    -k, --ksu [Y/n]                  Include KernelSU
    -r, --recovery [y/N]             Compile kernel for an Android Recovery
    -M, --manager [ksun/ksu/sukisu]  Root manager to use (default: ksun)
    -A, --android-ver [14/15/16]     Target Android OS version (default: 14)
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
        --manager|-M)
            MANAGER="$2"
            shift 2
            ;;
        --android-ver|-A)
            ANDROID_VER="$2"
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

# Ensure 'clang' binary/symlink exists alongside clang-18
if [ -f "$CLANG_DIR/bin/clang-18" ] && [ ! -e "$CLANG_DIR/bin/clang" ]; then
    ln -sf clang-18 "$CLANG_DIR/bin/clang"
    echo "Created clang -> clang-18 symlink"
fi

MAKE_FLAGS=(
  LLVM=1
  LLVM_IAS=1
  ARCH=arm64
  O=out
  HOSTCC=gcc
  HOSTCXX=g++
)

# ccache: inject CC/CXX wrappers when USE_CCACHE=1 is set by CI
if [ -n "${USE_CCACHE:-}" ] && command -v ccache &>/dev/null; then
  echo "ccache enabled — dir: ${CCACHE_DIR:-~/.ccache}"
  ccache --zero-stats 2>/dev/null || true
  _CLANG_ABS="$(cd "$CLANG_DIR" && pwd)"
  mkdir -p /tmp/ccwrap
  for _b in clang clang++ clang-18; do
    [ -e "$_CLANG_ABS/bin/$_b" ] || continue
    printf '#!/bin/sh\nexec ccache "%s" "$@"\n' "$_CLANG_ABS/bin/$_b" > "/tmp/ccwrap/$_b"
    chmod +x "/tmp/ccwrap/$_b"
  done
  export PATH="/tmp/ccwrap:$PATH"
  echo "ccache wrappers active: $(ls /tmp/ccwrap | tr '\n' ' ')"
fi

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

if [[ "${KSU_OPTION,,}" == "y" ]]; then
    KSU=ksu.config
    # Select manager submodule dir and re-point drivers/kernelsu symlink
    case "${MANAGER:-ksun}" in
        ksu)    MANAGER_DIR="KernelSU" ;;
        sukisu) MANAGER_DIR="SukiSU" ;;
        *)      MANAGER_DIR="KernelSU-Next" ;;
    esac
    echo "-----------------------------------------------"
    echo "Manager: ${MANAGER:-ksun} → drivers/kernelsu → ../$MANAGER_DIR/kernel"
    echo "-----------------------------------------------"
    rm -f drivers/kernelsu
    ln -sf "../${MANAGER_DIR}/kernel" drivers/kernelsu
fi

# Android OS version mapping for mkbootimg
# Determines os_version and os_patch_level used in the boot image header
case "${ANDROID_VER:-16}" in
    16) OS_VERSION=16.0.0; OS_PATCH_LEVEL=2025-06 ;;
    15) OS_VERSION=15.0.0; OS_PATCH_LEVEL=2025-05 ;;
    *)  OS_VERSION=14.0.0; OS_PATCH_LEVEL=2025-01 ;;
esac

echo "-----------------------------------------------"
echo "Android target: ${ANDROID_VER:-14} → os_version=$OS_VERSION, os_patch_level=$OS_PATCH_LEVEL"
echo "-----------------------------------------------"

rm -rf build/out/$MODEL
mkdir -p build/out/$MODEL/zip/files
mkdir -p build/out/$MODEL/zip/META-INF/com/google/android

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

# Build kernel image
echo "-----------------------------------------------"
echo "Defconfig: "$KERNEL_DEFCONFIG""

if [ -z "$KSU" ]; then
    echo "KSU: No"
else
    echo "KSU: Yes (manager: ${MANAGER:-ksun})"
fi

if [ -z "$RECOVERY" ]; then
    echo "Recovery: N"
else
    echo "Recovery: Y"
fi

echo "-----------------------------------------------"
echo "Building kernel using "$KERNEL_DEFCONFIG""
KCFLAGS_EXTRA="-O3"
KCFLAGS_EXTRA+=" -march=armv8.2-a+crypto+crc"
KCFLAGS_EXTRA+=" -mtune=cortex-a75"
KCFLAGS_EXTRA+=" -fno-semantic-interposition"
KCFLAGS_EXTRA+=" -fmerge-all-constants"

if echo "int f(void){return 0;}" | clang -mllvm -polly -c -x c - -o /dev/null 2>/dev/null; then
  echo "Polly available — enabling polyhedral loop optimizer"
  KCFLAGS_EXTRA+=" -mllvm -polly"
else
  echo "Polly not available in this toolchain — skipping"
fi
MAKE_FLAGS+=("KCFLAGS=${KCFLAGS_EXTRA}")

echo "-----------------------------------------------"
echo "Advanced flags: ${KCFLAGS_EXTRA}"
echo "-----------------------------------------------"

echo "Generating configuration file..."
echo "-----------------------------------------------"
make "${MAKE_FLAGS[@]}" -j$CORES exynos9820_defconfig $MODEL.config $KSU $RECOVERY || abort

echo "Building kernel..."
echo "-----------------------------------------------"
make "${MAKE_FLAGS[@]}" -j$CORES || abort

# Define constant variables
KERNEL_PATH=build/out/$MODEL/Image
KERNEL_OFFSET=0x00008000
RAMDISK_OFFSET=0xF0000000
SECOND_OFFSET=0xF0000000
TAGS_OFFSET=0x00000100
BASE=0x10000000
CMDLINE='loop.max_part=7'
HASHTYPE=sha1
HEADER_VERSION=1
PAGESIZE=2048
RAMDISK=build/out/$MODEL/ramdisk.cpio.gz
OUTPUT_FILE=build/out/$MODEL/boot.img

cp out/arch/arm64/boot/Image build/out/$MODEL

echo "-----------------------------------------------"
if [[ "$SOC" == "exynos9820" ]]; then
    echo "Building common exynos9820 Device Tree Blob Image..."
    echo "-----------------------------------------------"
    ./toolchain/mkdtimg cfg_create build/out/$MODEL/dtb.img build/dtconfigs/exynos9820.cfg -d out/arch/arm64/boot/dts/exynos
fi

if [[ "$SOC" == "exynos9825" ]]; then
    echo "Building common exynos9825 Device Tree Blob Image..."
    echo "-----------------------------------------------"
    ./toolchain/mkdtimg cfg_create build/out/$MODEL/dtb.img build/dtconfigs/exynos9825.cfg -d out/arch/arm64/boot/dts/exynos
fi
echo "-----------------------------------------------"

echo "Building Device Tree Blob Output Image for "$MODEL"..."
echo "-----------------------------------------------"
./toolchain/mkdtimg cfg_create build/out/$MODEL/dtbo.img build/dtconfigs/$MODEL.cfg -d out/arch/arm64/boot/dts/samsung
echo "-----------------------------------------------"

if [ -z "$RECOVERY" ]; then
    echo "Building RAMDisk..."
    echo "-----------------------------------------------"
    pushd build/ramdisk > /dev/null
    find . ! -name . | LC_ALL=C sort | cpio -o -H newc -R root:root | gzip > ../out/$MODEL/ramdisk.cpio.gz || abort
    popd > /dev/null
    echo "-----------------------------------------------"

    echo "Creating boot image (Android $OS_VERSION)..."
    echo "-----------------------------------------------"
    ./toolchain/mkbootimg --base $BASE --board $BOARD --cmdline "$CMDLINE" --hashtype $HASHTYPE \
    --header_version $HEADER_VERSION --kernel $KERNEL_PATH --kernel_offset $KERNEL_OFFSET \
    --os_patch_level $OS_PATCH_LEVEL --os_version $OS_VERSION --pagesize $PAGESIZE \
    --ramdisk $RAMDISK --ramdisk_offset $RAMDISK_OFFSET --second_offset $SECOND_OFFSET \
    --tags_offset $TAGS_OFFSET -o $OUTPUT_FILE || abort

    echo "Building zip..."
    echo "-----------------------------------------------"
    cp build/out/$MODEL/boot.img build/out/$MODEL/zip/files/boot.img
    cp build/out/$MODEL/dtb.img build/out/$MODEL/zip/files/dtb.img
    cp build/out/$MODEL/dtbo.img build/out/$MODEL/zip/files/dtbo.img
    cp build/update-binary build/out/$MODEL/zip/META-INF/com/google/android/update-binary
    cp build/updater-script build/out/$MODEL/zip/META-INF/com/google/android/updater-script

    pushd build/out/$MODEL/zip > /dev/null
    zip -r ../ExtremeKRNL-Nexus-"$MODEL".zip .
    popd > /dev/null
fi

popd > /dev/null
[ -n "${USE_CCACHE:-}" ] && ccache --show-stats 2>/dev/null || true
echo "Build finished successfully!"

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
    # SukiSU Kconfig declares "depends on KPROBES" — without CONFIG_KPROBES=y
    # the resolver silently drops CONFIG_KSU and SukiSU never initialises.
    # sukisu.config enables KPROBES; ksu.config uses KSUN manual-hooks.
    case "${MANAGER:-ksun}" in
        sukisu) KSU=sukisu.config ;;
        *)      KSU=ksu.config    ;;
    esac
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

    # ── SukiSU kernel 4.14 compatibility patches ──────────────────────────────
    # SukiSU targets Android GKI (5.10+). Two of its APIs are missing on 4.14:
    # 1. syscall_fn_t typedef  (added in 5.0 for ARM64 in asm/syscall.h)
    # 2. MODULE_IMPORT_NS macro (added in 5.4 in linux/module.h)
    # We patch the source in-tree before compilation. Both are no-op fixes:
    # removing them has zero runtime effect on 4.14.
    if [[ "${MANAGER_DIR}" == "SukiSU" ]]; then
        SHOOK="${MANAGER_DIR}/kernel/hook/syscall_hook.h"
        INITC="${MANAGER_DIR}/kernel/core/init.c"

        # Fix 1: Add ARM64 syscall_fn_t typedef (absent in asm/syscall.h < 5.0)
        node -e "
const fs = require('fs');
let c = fs.readFileSync('${SHOOK}','utf8');
if (!c.includes('__aarch64__')) {
    c = c.replace(
        '#include <asm/syscall.h>',
        '#include <asm/syscall.h>\n/* ARM64 kernel 4.14 compat */\n#if defined(__aarch64__) && !defined(__ksu_syscall_fn_t)\n#define __ksu_syscall_fn_t\ntypedef long (*syscall_fn_t)(const struct pt_regs *);\n#endif'
    );
    fs.writeFileSync('${SHOOK}', c);
    console.log('SukiSU patch 1: syscall_fn_t typedef added for ARM64 4.14');
}
"
        # Fix 2: Guard MODULE_IMPORT_NS — macro added in kernel 5.4, absent in 4.14.
        # Without the guard the compiler sees an undeclared identifier and aborts.
        node -e "
const fs = require('fs');
let c = fs.readFileSync('${INITC}','utf8');
const OLD = '#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)\nMODULE_IMPORT_NS(\"VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver\");\n#else\nMODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);\n#endif';
const NEW = '/* MODULE_IMPORT_NS added in 5.4 — guarded for 4.14 builds */\n#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)\nMODULE_IMPORT_NS(\"VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver\");\n#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)\nMODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);\n#endif';
if (c.includes(OLD)) {
    fs.writeFileSync('${INITC}', c.replace(OLD, NEW));
    console.log('SukiSU patch 2: MODULE_IMPORT_NS guarded for kernel 4.14');
} else {
    // Fallback: define it as empty if not already defined
    const FALLBACK = '#ifndef MODULE_IMPORT_NS\n#define MODULE_IMPORT_NS(ns)\n#endif';
    if (!c.includes(FALLBACK)) {
        c = c.replace('#include <linux/module.h>', '#include <linux/module.h>\n' + FALLBACK);
        fs.writeFileSync('${INITC}', c);
        console.log('SukiSU patch 2 (fallback): MODULE_IMPORT_NS stubbed for kernel 4.14');
    }
}
"

        # Fix 3: linux/pgtable.h was split out of asm/pgtable.h in kernel 5.8.
        # On 4.14, pgtable symbols come from asm/pgtable.h (via linux/mm.h).
        # Scan all SukiSU .c/.h files and replace the include with a version guard.
        find "${MANAGER_DIR}/kernel" \( -name "*.c" -o -name "*.h" \) | xargs grep -l "linux/pgtable.h" 2>/dev/null | while read PGTF; do
            sed -i 's|#include <linux/pgtable.h>|#ifndef LINUX_VERSION_CODE\n#include <linux/version.h>\n#endif\n#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)\n#include <linux/pgtable.h>\n#else\n#include <asm/pgtable.h>\n#endif|' "${PGTF}"
            echo "SukiSU patch 3: pgtable.h guarded in ${PGTF}"
        done

        # Fix 4: lsm_hook.c type mismatches on kernel 4.14
        # On 4.14: security_hook_list.list is list_head (not hlist_node),
        # and security_hook_list.head is list_head* (not hlist_head*).
        # These code paths are unreachable with CONFIG_KSU_MANUAL_HOOK=y on 4.14.
        LSM_HOOK="${MANAGER_DIR}/kernel/hook/lsm_hook.c"
        if [ -f "$LSM_HOOK" ]; then
            node -e "
const fs = require('fs');
let c = fs.readFileSync('${LSM_HOOK}', 'utf8');
let changed = false;

// Fix a: hook->list.head = head (list_head* <- hlist_head* type mismatch)
const OLD_A = 'hook->list.head = head;';
const NEW_A = 'hook->list.head = (struct list_head *)(void *)head;';
if (c.includes(OLD_A)) { c = c.replace(OLD_A, NEW_A); changed = true; }

// Fix b: hook->list.list.pprev does not exist on struct list_head in 4.14
const OLD_B = 'hook->list.list.pprev = &head->first;';
const NEW_B = '#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)\n                    hook->list.list.pprev = &head->first;\n#else\n                    /* 4.14: list_head has no pprev; manual hooks used */\n                    (void)head;\n#endif';
if (c.includes(OLD_B)) { c = c.replace(OLD_B, NEW_B); changed = true; }

// Fix c: hook->list.head->first — list_head has no .first member
const OLD_C = 'slot = (void **)&hook->list.head->first;';
const NEW_C = 'slot = (void **)&((struct hlist_head *)(void *)hook->list.head)->first;';
if (c.includes(OLD_C)) { c = c.replace(OLD_C, NEW_C); changed = true; }

if (changed) { fs.writeFileSync('${LSM_HOOK}', c); console.log('SukiSU patch 4: lsm_hook.c 4.14 type compat fixed'); }
else { console.log('SukiSU patch 4: no patterns found in lsm_hook.c'); }
"
        fi

        # Fix 5: patch_memory.c — arm64 cache flush + nofault write API compat for 4.14
        # __flush_icache_range renamed/added ~5.4; copy_to_kernel_nofault added in 5.8
        PMEM="${MANAGER_DIR}/kernel/hook/arm64/patch_memory.c"
        if [ -f "$PMEM" ]; then
            node -e "
const fs = require('fs');
let c = fs.readFileSync('${PMEM}', 'utf8');
let changed = false;

// Fix a: __flush_icache_range -> flush_icache_range on 4.14
const OLD_ICACHE = '#define ksu_flush_icache(start, end) __flush_icache_range';
const NEW_ICACHE = '#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)\n#define ksu_flush_icache(start, end) __flush_icache_range\n#else\n#define ksu_flush_icache(start, end) flush_icache_range\n#endif';
if (c.includes(OLD_ICACHE)) { c = c.replace(OLD_ICACHE, NEW_ICACHE); changed = true; }

// Fix b: copy_to_kernel_nofault -> probe_kernel_write on 4.14 (added in 5.8)
const OLD_NOFAULT = 'ret = (int)copy_to_kernel_nofault(map, src, len);';
const NEW_NOFAULT = '#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)\n    ret = (int)copy_to_kernel_nofault(map, src, len);\n#else\n    ret = (int)probe_kernel_write(map, src, len);\n#endif';
if (c.includes(OLD_NOFAULT)) { c = c.replace(OLD_NOFAULT, NEW_NOFAULT); changed = true; }

if (changed) { fs.writeFileSync('${PMEM}', c); console.log('SukiSU patch 5: patch_memory.c 4.14 cache/nofault compat fixed'); }
else { console.log('SukiSU patch 5: patterns not found in patch_memory.c'); }
"
        fi

        # Fix 6: file_wrapper.c — multiple struct file_operations fields absent on kernel 4.14
        # __poll_t (4.16+), iopoll (5.1+), remap_file_range/REMAP_FILE_DEDUP (4.20+), fadvise/mmap_supported_flags (5.0+)
        cat > /tmp/ksu_fw_patch.js << 'FWPATCH'
const fs = require('fs');
const FWRAP = 'SukiSU/kernel/infra/file_wrapper.c';
if (!fs.existsSync(FWRAP)) { console.log('SukiSU patch 6: file_wrapper.c not found at', FWRAP); process.exit(0); }
let c = fs.readFileSync(FWRAP, 'utf8');
let n = 0;

// Fix a: add __poll_t typedef for kernel < 4.16
if (c.includes('#include <linux/version.h>') && !c.includes('typedef unsigned int __poll_t')) {
  c = c.replace('#include <linux/version.h>',
    '#include <linux/version.h>\n#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0)\ntypedef unsigned int __poll_t;\n#endif');
  n++;
}

// Fix b: guard iopoll function #else -> #elif >= 5.1 (iopoll absent on 4.14)
if (c.includes('#else\nstatic int ksu_wrapper_iopoll(struct kiocb *kiocb, bool spin)')) {
  c = c.replace('#else\nstatic int ksu_wrapper_iopoll(struct kiocb *kiocb, bool spin)',
    '#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)\nstatic int ksu_wrapper_iopoll(struct kiocb *kiocb, bool spin)');
  n++;
}

// Fix c: guard iopoll struct assignment >= 5.1
if (c.includes('    p->ops.iopoll = fp->f_op->iopoll ? ksu_wrapper_iopoll : NULL;')) {
  c = c.replace('    p->ops.iopoll = fp->f_op->iopoll ? ksu_wrapper_iopoll : NULL;',
    '#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)\n    p->ops.iopoll = fp->f_op->iopoll ? ksu_wrapper_iopoll : NULL;\n#endif');
  n++;
}

// Fix d: guard mmap_supported_flags — restrict #else to >= 5.0
if (c.includes('#else\n    p->ops.mmap_supported_flags = fp->f_op->mmap_supported_flags;\n#endif')) {
  c = c.replace('#else\n    p->ops.mmap_supported_flags = fp->f_op->mmap_supported_flags;\n#endif',
    '#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)\n    p->ops.mmap_supported_flags = fp->f_op->mmap_supported_flags;\n#endif');
  n++;
}

// Fix e: guard remap_file_range + fadvise assignments
const OLD_REMAP = '    p->ops.remap_file_range = fp->f_op->remap_file_range ? ksu_wrapper_remap_file_range : NULL;\n    p->ops.fadvise = fp->f_op->fadvise ? ksu_wrapper_fadvise : NULL;';
if (c.includes(OLD_REMAP)) {
  c = c.replace(OLD_REMAP,
    '#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)\n    p->ops.remap_file_range = fp->f_op->remap_file_range ? ksu_wrapper_remap_file_range : NULL;\n#endif\n#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)\n    p->ops.fadvise = fp->f_op->fadvise ? ksu_wrapper_fadvise : NULL;\n#endif');
  n++;
}

// Fix f: wrap remap_file_range function definition >= 4.20
const RF_START = 'static loff_t ksu_wrapper_remap_file_range(';
const RF_GUARD = '#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)\nstatic loff_t ksu_wrapper_remap_file_range(';
if (c.includes(RF_START) && !c.includes(RF_GUARD)) {
  const si2 = c.indexOf(RF_START);
  const ei2 = c.indexOf('\n}\n\nstatic int ksu_wrapper_fadvise(', si2);
  if (si2 > 0 && ei2 > si2) {
    c = c.slice(0, si2) + '#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)\n' +
        c.slice(si2, ei2 + 3) + '\n#endif\n' + c.slice(ei2 + 3); n++;
  }
}

// Fix g: wrap fadvise function definition >= 5.0
const FA_START = 'static int ksu_wrapper_fadvise(struct file *fp,';
const FA_GUARD = '#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)\nstatic int ksu_wrapper_fadvise(';
if (c.includes(FA_START) && !c.includes(FA_GUARD)) {
  const si3 = c.indexOf(FA_START);
  const ei3 = c.indexOf('\n}\n\nstatic void ksu_release_file_wrapper(', si3);
  if (si3 > 0 && ei3 > si3) {
    c = c.slice(0, si3) + '#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)\n' +
        c.slice(si3, ei3 + 3) + '\n#endif\n' + c.slice(ei3 + 3); n++;
  }
}

fs.writeFileSync(FWRAP, c);
console.log('SukiSU patch 6: file_wrapper.c 4.14 compat fixed (' + n + ' replacements)');
FWPATCH
        node /tmp/ksu_fw_patch.js
        fi
    fi
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

# Apply susfs kernel patches — manager-aware
# KSUN  : patch uses KSUN symbols (__ksu_is_allow_uid, setup_selinux) — apply it
# SukiSU: susfs already built-in inside SukiSU/kernel/ — no external patch needed
# KSU   : KSUN patch symbols not in KSU vanilla — skip to avoid link errors
echo "-----------------------------------------------"
echo "susfs patching — manager: ${MANAGER:-ksun}"
echo "-----------------------------------------------"
case "${MANAGER:-ksun}" in
    ksun)
        SUSFS_PATCH="build/patches/50_add_susfs_in_kernel-4.14.patch"
        if [ -f "$SUSFS_PATCH" ]; then
            patch -p1 --forward --batch < "$SUSFS_PATCH" \
                && echo "susfs patches applied." \
                || echo "susfs patches already applied or skipped."
        else
            echo "susfs patch not found — skipping."
        fi
        ;;
    sukisu)
        echo "SukiSU has susfs built-in — no external patch needed."
        ;;
    ksu)
        echo "KSU vanilla — KSUN susfs patch incompatible (symbol mismatch) — skipping."
        ;;
esac
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

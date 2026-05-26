/* SPDX-License-Identifier: GPL-2.0 */
/*
 * extreme-tweaker.kpm  v2.2  -  ExtremeKernel Network & Memory Battery Tweaker
 * Samsung Galaxy S20 Ultra (d2s, Exynos 9825), kernel 4.14
 *
 * All values are saved on load and fully restored on unload.
 *
 * Tweaks applied:
 *   vm_dirty_bytes / vm_dirty_background_bytes -> 0
 *       Samsung sets these non-zero which silently overrides the ratio-based
 *       dirty controls.  Must be zeroed first.
 *
 *       Primary path:  kallsyms_lookup_name (direct kernel symbol, fastest)
 *       Fallback path: write "0" to /proc/sys/vm/dirty_background_bytes via
 *                      kernel file-I/O.  This works even when Samsung strips
 *                      the symbol from kallsyms, because the sysctl proc entry
 *                      is always present.  This is why "echo 0 > ..." from root
 *                      works but the old module did not.
 *
 *   vm_dirty_ratio                -> 30%
 *   vm_dirty_background_ratio     -> 8%
 *   sysctl_tcp_fastopen           -> 3  (skipped if CONFIG_TCP_FASTOPEN absent)
 *   sysctl_tcp_slow_start_after_idle -> 0
 *
 * Changelog:
 *   v2.2 - Add sysfs /proc fallback for dirty_background_bytes so the
 *          dirty_background_ratio actually shows 8 instead of 0.
 *          All key messages use pr_warn for dmesg visibility.
 */

#include <compiler.h>
#include <kpmodule.h>
#include <kputils.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <kallsyms.h>

KPM_NAME("extreme-tweaker");
KPM_VERSION("2.2.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("ExtremeKernel");
KPM_DESCRIPTION("Battery-focused memory+network tweaks for d2s. Full restore on unload.");

/* ============================================================
 * KERNEL FUNCTION TYPES (resolved via kallsyms at runtime)
 * ============================================================ */
typedef struct file *(*fn_filp_open_t)(const char *, int, umode_t);
typedef int          (*fn_filp_close_t)(struct file *, void *);
typedef ssize_t      (*fn_kernel_write_t)(struct file *, const void *, size_t, loff_t *);
typedef ssize_t      (*fn_kernel_read_t)(struct file *, void *, size_t, loff_t *);
typedef int          (*fn_snprintf_t)(char *, size_t, const char *, ...);

static fn_filp_open_t    kfn_filp_open    = 0;
static fn_filp_close_t   kfn_filp_close   = 0;
static fn_kernel_write_t kfn_kernel_write = 0;
static fn_kernel_read_t  kfn_kernel_read  = 0;
static fn_snprintf_t     kfn_snprintf     = 0;

/* IS_ERR equivalent */
static int is_err_ptr(void *p) {
    return (unsigned long)p >= (unsigned long)-4096UL;
}

/* Write a decimal number string to a sysctl /proc/sys path.
 * Returns 1 on success, 0 on failure.
 * This is the fallback when kallsyms can't find the symbol. */
static int sysctl_write_ulong(const char *path, unsigned long val)
{
    struct file *f;
    char buf[32];
    int len = 0;
    loff_t pos = 0;
    ssize_t n;

    if (!kfn_filp_open || !kfn_kernel_write || !kfn_filp_close || !kfn_snprintf)
        return 0;

    len = kfn_snprintf(buf, sizeof(buf), "%lu\n", val);
    if (len <= 0) return 0;

    f = kfn_filp_open(path, 1 /* O_WRONLY */, 0);
    if (!f || is_err_ptr(f)) {
        pr_warn("[EK-TWEAK] sysctl_write: open failed: %s\n", path);
        return 0;
    }
    n = kfn_kernel_write(f, buf, len, &pos);
    kfn_filp_close(f, 0);
    return (n > 0);
}

/* Read a decimal number from a sysctl /proc/sys path.
 * Returns the value, or 0xFFFFFFFFUL on failure. */
static unsigned long sysctl_read_ulong(const char *path)
{
    struct file *f;
    char buf[32];
    loff_t pos = 0;
    ssize_t n;
    unsigned long val = 0;
    int i;

    if (!kfn_filp_open || !kfn_kernel_read || !kfn_filp_close) return 0xFFFFFFFFUL;

    f = kfn_filp_open(path, 0 /* O_RDONLY */, 0);
    if (!f || is_err_ptr(f)) return 0xFFFFFFFFUL;

    n = kfn_kernel_read(f, buf, sizeof(buf) - 1, &pos);
    kfn_filp_close(f, 0);

    if (n <= 0) return 0xFFFFFFFFUL;
    buf[n] = '\0';

    for (i = 0; buf[i] >= '0' && buf[i] <= '9'; i++)
        val = val * 10 + (buf[i] - '0');
    return val;
}

/* ============================================================
 * KERNEL VARIABLE POINTERS (resolved via kallsyms)
 * ============================================================ */
static int           *p_dirty_ratio         = 0;
static int           *p_dirty_bg_ratio      = 0;
static unsigned long *p_dirty_bytes         = 0;
static unsigned long *p_dirty_bg_bytes      = 0;
static unsigned int  *p_tcp_fastopen        = 0;
static int           *p_tcp_slow_start_idle = 0;

/* Saved originals */
static int            orig_dirty_ratio         = 20;
static int            orig_dirty_bg_ratio      = 10;
static unsigned long  orig_dirty_bytes         = 0;
static unsigned long  orig_dirty_bg_bytes      = 0;
static unsigned int   orig_tcp_fastopen        = 1;
static int            orig_tcp_slow_start_idle = 1;

/* Track which restore method to use */
static int dirty_bytes_via_sysctl    = 0;  /* 1 = restored via /proc/sys */
static int dirty_bg_bytes_via_sysctl = 0;

/* ============================================================
 * INIT
 * ============================================================ */
static long tweaker_init(const char *args, const char *event, void *__user reserved)
{
    pr_warn("[EK-TWEAK] extreme-tweaker v2.2 loading...\n");

    /* Resolve file-I/O functions for sysctl fallback */
    kfn_filp_open    = (fn_filp_open_t)   kallsyms_lookup_name("filp_open");
    kfn_filp_close   = (fn_filp_close_t)  kallsyms_lookup_name("filp_close");
    kfn_kernel_write = (fn_kernel_write_t) kallsyms_lookup_name("kernel_write");
    kfn_kernel_read  = (fn_kernel_read_t)  kallsyms_lookup_name("kernel_read");
    kfn_snprintf     = (fn_snprintf_t)     kallsyms_lookup_name("snprintf");

    /* Resolve direct kernel symbols */
    p_dirty_ratio         = (int *)kallsyms_lookup_name("vm_dirty_ratio");
    p_dirty_bg_ratio      = (int *)kallsyms_lookup_name("vm_dirty_background_ratio");
    p_dirty_bytes         = (unsigned long *)kallsyms_lookup_name("vm_dirty_bytes");
    p_dirty_bg_bytes      = (unsigned long *)kallsyms_lookup_name("vm_dirty_background_bytes");
    p_tcp_fastopen        = (unsigned int *)kallsyms_lookup_name("sysctl_tcp_fastopen");
    p_tcp_slow_start_idle = (int *)kallsyms_lookup_name("sysctl_tcp_slow_start_after_idle");

    pr_warn("[EK-TWEAK] sym vm_dirty_ratio              %s\n", p_dirty_ratio         ? "OK" : "MISSING");
    pr_warn("[EK-TWEAK] sym vm_dirty_background_ratio   %s\n", p_dirty_bg_ratio      ? "OK" : "MISSING");
    pr_warn("[EK-TWEAK] sym vm_dirty_bytes              %s\n", p_dirty_bytes         ? "OK" : "MISSING");
    pr_warn("[EK-TWEAK] sym vm_dirty_background_bytes   %s\n", p_dirty_bg_bytes      ? "OK" : "MISSING");
    pr_warn("[EK-TWEAK] sym sysctl_tcp_fastopen         %s\n", p_tcp_fastopen        ? "OK" : "MISSING");
    pr_warn("[EK-TWEAK] sym sysctl_tcp_slow_start_idle  %s\n", p_tcp_slow_start_idle ? "OK" : "MISSING");
    pr_warn("[EK-TWEAK] fn  filp_open/filp_close/kernel_write: %s/%s/%s\n",
            kfn_filp_open    ? "OK" : "NO",
            kfn_filp_close   ? "OK" : "NO",
            kfn_kernel_write ? "OK" : "NO");

    /* -------------------------------------------------------
     * vm_dirty_bytes
     * Primary: kallsyms. Fallback: /proc/sys/vm/dirty_bytes
     * ------------------------------------------------------- */
    if (p_dirty_bytes) {
        orig_dirty_bytes = *p_dirty_bytes;
        *p_dirty_bytes = 0;
        pr_warn("[EK-TWEAK] vm_dirty_bytes (kallsyms): %lu -> 0\n", orig_dirty_bytes);
    } else {
        orig_dirty_bytes = sysctl_read_ulong("/proc/sys/vm/dirty_bytes");
        if (orig_dirty_bytes != 0xFFFFFFFFUL && sysctl_write_ulong("/proc/sys/vm/dirty_bytes", 0)) {
            dirty_bytes_via_sysctl = 1;
            pr_warn("[EK-TWEAK] vm_dirty_bytes (sysctl fallback): %lu -> 0 OK\n", orig_dirty_bytes);
        } else {
            pr_warn("[EK-TWEAK] vm_dirty_bytes: symbol MISSING and sysctl write failed\n");
        }
    }

    /* -------------------------------------------------------
     * vm_dirty_background_bytes  — THIS is why dirty_bg_ratio
     * was stuck at 0: Samsung sets this non-zero, overriding
     * the ratio. Must be zeroed before setting the ratio.
     * Primary: kallsyms. Fallback: /proc/sys/vm/dirty_background_bytes
     * ------------------------------------------------------- */
    if (p_dirty_bg_bytes) {
        orig_dirty_bg_bytes = *p_dirty_bg_bytes;
        *p_dirty_bg_bytes = 0;
        pr_warn("[EK-TWEAK] vm_dirty_background_bytes (kallsyms): %lu -> 0\n", orig_dirty_bg_bytes);
    } else {
        orig_dirty_bg_bytes = sysctl_read_ulong("/proc/sys/vm/dirty_background_bytes");
        if (orig_dirty_bg_bytes != 0xFFFFFFFFUL && sysctl_write_ulong("/proc/sys/vm/dirty_background_bytes", 0)) {
            dirty_bg_bytes_via_sysctl = 1;
            pr_warn("[EK-TWEAK] vm_dirty_background_bytes (sysctl fallback): %lu -> 0 OK\n", orig_dirty_bg_bytes);
            pr_warn("[EK-TWEAK] dirty_background_ratio should now read 8 in /proc (bytes was overriding it)\n");
        } else {
            pr_warn("[EK-TWEAK] vm_dirty_background_bytes: MISSING + sysctl failed — ratio will stay 0\n");
        }
    }

    /* -------------------------------------------------------
     * vm_dirty_ratio / vm_dirty_background_ratio
     * ------------------------------------------------------- */
    if (p_dirty_ratio) {
        orig_dirty_ratio = *p_dirty_ratio;
        *p_dirty_ratio = 30;
        pr_warn("[EK-TWEAK] vm_dirty_ratio: %d -> 30%%\n", orig_dirty_ratio);
    }
    if (p_dirty_bg_ratio) {
        orig_dirty_bg_ratio = *p_dirty_bg_ratio;
        *p_dirty_bg_ratio = 8;
        pr_warn("[EK-TWEAK] vm_dirty_background_ratio: %d -> 8%%\n", orig_dirty_bg_ratio);
    }

    /* -------------------------------------------------------
     * TCP tweaks
     * ------------------------------------------------------- */
    if (p_tcp_fastopen) {
        orig_tcp_fastopen = *p_tcp_fastopen;
        *p_tcp_fastopen = 3;
        pr_warn("[EK-TWEAK] sysctl_tcp_fastopen: %u -> 3\n", orig_tcp_fastopen);
    } else {
        pr_warn("[EK-TWEAK] sysctl_tcp_fastopen: MISSING (CONFIG_TCP_FASTOPEN not in Samsung build)\n");
    }
    if (p_tcp_slow_start_idle) {
        orig_tcp_slow_start_idle = *p_tcp_slow_start_idle;
        *p_tcp_slow_start_idle = 0;
        pr_warn("[EK-TWEAK] sysctl_tcp_slow_start_idle: %d -> 0\n", orig_tcp_slow_start_idle);
    }

    pr_warn("[EK-TWEAK] LOADED v2.2. Unload to restore all originals.\n");
    return 0;
}

/* ============================================================
 * CTL0
 * ============================================================ */
static long tweaker_ctl0(const char *args, char *__user out_msg, int outlen)
{
    char buf[768];
    int n = 0;
    if (kfn_snprintf) {
        n = kfn_snprintf(buf, sizeof(buf),
            "extreme-tweaker v2.2 active\n"
            "  vm_dirty_bytes             = %lu (was %lu, via %s)\n"
            "  vm_dirty_background_bytes  = %lu (was %lu, via %s)\n"
            "  vm_dirty_ratio             = %d%% (was %d%%)\n"
            "  vm_dirty_background_ratio  = %d%% (was %d%%)\n"
            "  sysctl_tcp_fastopen        = %u (was %u)\n"
            "  sysctl_tcp_slow_start      = %d (was %d)\n"
            "\nCheck: su -c \"dmesg | grep EK-TWEAK\"\n"
            "Check: su -c \"cat /proc/sys/vm/dirty_background_ratio\"\n",
            p_dirty_bytes    ? *p_dirty_bytes    : 0, orig_dirty_bytes,
            p_dirty_bytes    ? "kallsyms" : (dirty_bytes_via_sysctl ? "sysctl" : "FAILED"),
            p_dirty_bg_bytes ? *p_dirty_bg_bytes : 0, orig_dirty_bg_bytes,
            p_dirty_bg_bytes ? "kallsyms" : (dirty_bg_bytes_via_sysctl ? "sysctl" : "FAILED"),
            p_dirty_ratio         ? *p_dirty_ratio    : -1, orig_dirty_ratio,
            p_dirty_bg_ratio      ? *p_dirty_bg_ratio : -1, orig_dirty_bg_ratio,
            p_tcp_fastopen        ? *p_tcp_fastopen   : 0, orig_tcp_fastopen,
            p_tcp_slow_start_idle ? *p_tcp_slow_start_idle : -1, orig_tcp_slow_start_idle
        );
    } else {
        buf[0] = 'O'; buf[1] = 'K'; buf[2] = '\n'; buf[3] = '\0'; n = 3;
    }
    compat_copy_to_user(out_msg, buf, n + 1);
    return 0;
}

/* ============================================================
 * EXIT — restore everything
 * ============================================================ */
static long tweaker_exit(void *__user reserved)
{
    pr_warn("[EK-TWEAK] unloading — restoring ALL originals...\n");

    /* Restore dirty_bytes */
    if (p_dirty_bytes) {
        *p_dirty_bytes = orig_dirty_bytes;
        pr_warn("[EK-TWEAK] vm_dirty_bytes restored: %lu\n", orig_dirty_bytes);
    } else if (dirty_bytes_via_sysctl) {
        sysctl_write_ulong("/proc/sys/vm/dirty_bytes", orig_dirty_bytes);
        pr_warn("[EK-TWEAK] vm_dirty_bytes (sysctl) restored: %lu\n", orig_dirty_bytes);
    }

    /* Restore dirty_background_bytes */
    if (p_dirty_bg_bytes) {
        *p_dirty_bg_bytes = orig_dirty_bg_bytes;
        pr_warn("[EK-TWEAK] vm_dirty_background_bytes restored: %lu\n", orig_dirty_bg_bytes);
    } else if (dirty_bg_bytes_via_sysctl) {
        sysctl_write_ulong("/proc/sys/vm/dirty_background_bytes", orig_dirty_bg_bytes);
        pr_warn("[EK-TWEAK] vm_dirty_background_bytes (sysctl) restored: %lu\n", orig_dirty_bg_bytes);
    }

    /* Restore ratios */
    if (p_dirty_ratio)         { *p_dirty_ratio    = orig_dirty_ratio;         pr_warn("[EK-TWEAK] vm_dirty_ratio restored: %d\n", orig_dirty_ratio); }
    if (p_dirty_bg_ratio)      { *p_dirty_bg_ratio = orig_dirty_bg_ratio;      pr_warn("[EK-TWEAK] vm_dirty_background_ratio restored: %d\n", orig_dirty_bg_ratio); }
    if (p_tcp_fastopen)        { *p_tcp_fastopen   = orig_tcp_fastopen;        pr_warn("[EK-TWEAK] sysctl_tcp_fastopen restored: %u\n", orig_tcp_fastopen); }
    if (p_tcp_slow_start_idle) { *p_tcp_slow_start_idle = orig_tcp_slow_start_idle; pr_warn("[EK-TWEAK] sysctl_tcp_slow_start_idle restored: %d\n", orig_tcp_slow_start_idle); }

    pr_warn("[EK-TWEAK] UNLOADED cleanly.\n");
    return 0;
}

KPM_INIT(tweaker_init);
KPM_CTL0(tweaker_ctl0);
KPM_EXIT(tweaker_exit);

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * extreme-tweaker.kpm  v2.1  -  ExtremeKernel Network & Memory Battery Tweaker
 * Samsung Galaxy S20 Ultra (d2s, Exynos 9825), kernel 4.14
 *
 * All values are saved on load and fully restored on unload.
 *
 * Tweaks applied:
 *   vm_dirty_bytes / vm_dirty_background_bytes -> 0
 *       Samsung sets these non-zero which silently overrides the ratio-based
 *       dirty controls. Must be zeroed first. If the symbol is not in kallsyms
 *       (Samsung stripped it), a warning is logged but ratio tweaks still apply.
 *   vm_dirty_ratio                -> 30%  (less frequent background flushing)
 *   vm_dirty_background_ratio     -> 8%   (background writeback threshold)
 *   sysctl_tcp_fastopen           -> 3    (TFO client+server)
 *       NOTE: If /proc/sys/net/ipv4/tcp_fastopen does not exist, CONFIG_TCP_FASTOPEN
 *       is not compiled into this Samsung build — the tweak is skipped cleanly.
 *   sysctl_tcp_slow_start_after_idle -> 0
 *
 * Changelog:
 *   v2.1 - pr_warn for all key messages so they survive dmesg ring-buffer wrap
 *          and appear even when console_loglevel is restricted.
 *          Per-symbol OK/MISSING log lines added for easier diagnosis.
 *          dirty_background_ratio now logs current value after apply so you
 *          can tell if vm_dirty_background_bytes blocked it.
 */

#include <compiler.h>
#include <kpmodule.h>
#include <kputils.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <kallsyms.h>

KPM_NAME("extreme-tweaker");
KPM_VERSION("2.1.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("ExtremeKernel");
KPM_DESCRIPTION("Battery-focused memory+network tweaks for d2s. Full restore on unload.");

/* Pointers to kernel variables */
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

static int (*kpm_snprintf)(char *buf, size_t size, const char *fmt, ...) = 0;

static long tweaker_init(const char *args, const char *event, void *__user reserved)
{
    pr_warn("[EK-TWEAK] extreme-tweaker v2.1 loading...\n");

    kpm_snprintf = (typeof(kpm_snprintf))kallsyms_lookup_name("snprintf");

    /* Resolve all symbols — log OK/MISSING for each */
    p_dirty_ratio         = (int *)kallsyms_lookup_name("vm_dirty_ratio");
    p_dirty_bg_ratio      = (int *)kallsyms_lookup_name("vm_dirty_background_ratio");
    p_dirty_bytes         = (unsigned long *)kallsyms_lookup_name("vm_dirty_bytes");
    p_dirty_bg_bytes      = (unsigned long *)kallsyms_lookup_name("vm_dirty_background_bytes");
    p_tcp_fastopen        = (unsigned int *)kallsyms_lookup_name("sysctl_tcp_fastopen");
    p_tcp_slow_start_idle = (int *)kallsyms_lookup_name("sysctl_tcp_slow_start_after_idle");

    pr_warn("[EK-TWEAK] resolve vm_dirty_ratio              %s\n", p_dirty_ratio         ? "OK" : "MISSING");
    pr_warn("[EK-TWEAK] resolve vm_dirty_background_ratio   %s\n", p_dirty_bg_ratio      ? "OK" : "MISSING");
    pr_warn("[EK-TWEAK] resolve vm_dirty_bytes              %s\n", p_dirty_bytes         ? "OK" : "MISSING");
    pr_warn("[EK-TWEAK] resolve vm_dirty_background_bytes   %s\n", p_dirty_bg_bytes      ? "OK" : "MISSING");
    pr_warn("[EK-TWEAK] resolve sysctl_tcp_fastopen         %s\n", p_tcp_fastopen        ? "OK" : "MISSING");
    pr_warn("[EK-TWEAK] resolve sysctl_tcp_slow_start_idle  %s\n", p_tcp_slow_start_idle ? "OK" : "MISSING");

    /*
     * Samsung ships vm_dirty_bytes / vm_dirty_background_bytes != 0.
     * When non-zero the kernel ignores vm_dirty_ratio entirely and
     * /proc/sys/vm/dirty_ratio reads as 0.
     * Save originals then zero them so ratio-based control takes effect.
     *
     * If the symbol is MISSING (Samsung stripped it), we skip zeroing and
     * try the ratio anyway — it may still work if Samsung already has bytes=0.
     */
    if (p_dirty_bytes) {
        orig_dirty_bytes = *p_dirty_bytes;
        *p_dirty_bytes = 0;
        pr_warn("[EK-TWEAK] vm_dirty_bytes              %lu -> 0\n", orig_dirty_bytes);
    } else {
        pr_warn("[EK-TWEAK] vm_dirty_bytes NOT found — skipping zero step\n");
    }

    if (p_dirty_bg_bytes) {
        orig_dirty_bg_bytes = *p_dirty_bg_bytes;
        *p_dirty_bg_bytes = 0;
        pr_warn("[EK-TWEAK] vm_dirty_background_bytes   %lu -> 0\n", orig_dirty_bg_bytes);
    } else {
        pr_warn("[EK-TWEAK] vm_dirty_background_bytes NOT found — if dirty_bg_ratio stays 0 in /proc,\n");
        pr_warn("[EK-TWEAK]   Samsung's bytes value is still non-zero overriding the ratio.\n");
        pr_warn("[EK-TWEAK]   Run: su -c \"cat /proc/kallsyms | grep dirty_background_bytes\"\n");
    }

    if (p_dirty_ratio) {
        orig_dirty_ratio = *p_dirty_ratio;
        *p_dirty_ratio = 30;
        pr_warn("[EK-TWEAK] vm_dirty_ratio              %d -> 30%%\n", orig_dirty_ratio);
    }

    if (p_dirty_bg_ratio) {
        orig_dirty_bg_ratio = *p_dirty_bg_ratio;
        *p_dirty_bg_ratio = 8;
        pr_warn("[EK-TWEAK] vm_dirty_background_ratio   %d -> 8%% (reads 0 in /proc if bg_bytes still non-zero)\n",
                orig_dirty_bg_ratio);
    }

    if (p_tcp_fastopen) {
        orig_tcp_fastopen = *p_tcp_fastopen;
        *p_tcp_fastopen = 3;
        pr_warn("[EK-TWEAK] sysctl_tcp_fastopen         %u -> 3\n", orig_tcp_fastopen);
    } else {
        pr_warn("[EK-TWEAK] sysctl_tcp_fastopen MISSING — CONFIG_TCP_FASTOPEN not in Samsung build\n");
    }

    if (p_tcp_slow_start_idle) {
        orig_tcp_slow_start_idle = *p_tcp_slow_start_idle;
        *p_tcp_slow_start_idle = 0;
        pr_warn("[EK-TWEAK] sysctl_tcp_slow_start_idle  %d -> 0\n", orig_tcp_slow_start_idle);
    }

    pr_warn("[EK-TWEAK] LOADED v2.1. Unload to restore all originals.\n");
    return 0;
}

static long tweaker_ctl0(const char *args, char *__user out_msg, int outlen)
{
    char buf[768];
    int n = 0;
    if (kpm_snprintf) {
        n = kpm_snprintf(buf, sizeof(buf),
            "extreme-tweaker v2.1 — live values (orig):\n"
            "  vm_dirty_bytes             = %lu (was %lu)\n"
            "  vm_dirty_background_bytes  = %lu (was %lu)\n"
            "  vm_dirty_ratio             = %d%% (was %d%%)\n"
            "  vm_dirty_background_ratio  = %d%% (was %d%%)\n"
            "  sysctl_tcp_fastopen        = %u (was %u)\n"
            "  sysctl_tcp_slow_start      = %d (was %d)\n"
            "\nNOTE: if dirty_background_ratio shows 0 in /proc,\n"
            "vm_dirty_background_bytes is still non-zero (symbol not found).\n"
            "Check dmesg: su -c \"dmesg | grep EK-TWEAK\"\n",
            p_dirty_bytes         ? *p_dirty_bytes         : 0, orig_dirty_bytes,
            p_dirty_bg_bytes      ? *p_dirty_bg_bytes      : 0, orig_dirty_bg_bytes,
            p_dirty_ratio         ? *p_dirty_ratio         : -1, orig_dirty_ratio,
            p_dirty_bg_ratio      ? *p_dirty_bg_ratio      : -1, orig_dirty_bg_ratio,
            p_tcp_fastopen        ? *p_tcp_fastopen        : 0, orig_tcp_fastopen,
            p_tcp_slow_start_idle ? *p_tcp_slow_start_idle : -1, orig_tcp_slow_start_idle
        );
    } else {
        buf[0] = 'O'; buf[1] = 'K'; buf[2] = '\n'; buf[3] = '\0';
        n = 3;
    }
    compat_copy_to_user(out_msg, buf, n + 1);
    return 0;
}

static long tweaker_exit(void *__user reserved)
{
    pr_warn("[EK-TWEAK] unloading — restoring ALL original values...\n");

    if (p_dirty_bytes)         { *p_dirty_bytes         = orig_dirty_bytes;         pr_warn("[EK-TWEAK] vm_dirty_bytes restored:            %lu\n", orig_dirty_bytes); }
    if (p_dirty_bg_bytes)      { *p_dirty_bg_bytes      = orig_dirty_bg_bytes;      pr_warn("[EK-TWEAK] vm_dirty_background_bytes restored: %lu\n", orig_dirty_bg_bytes); }
    if (p_dirty_ratio)         { *p_dirty_ratio         = orig_dirty_ratio;         pr_warn("[EK-TWEAK] vm_dirty_ratio restored:            %d\n", orig_dirty_ratio); }
    if (p_dirty_bg_ratio)      { *p_dirty_bg_ratio      = orig_dirty_bg_ratio;      pr_warn("[EK-TWEAK] vm_dirty_background_ratio restored: %d\n", orig_dirty_bg_ratio); }
    if (p_tcp_fastopen)        { *p_tcp_fastopen        = orig_tcp_fastopen;        pr_warn("[EK-TWEAK] sysctl_tcp_fastopen restored:        %u\n", orig_tcp_fastopen); }
    if (p_tcp_slow_start_idle) { *p_tcp_slow_start_idle = orig_tcp_slow_start_idle; pr_warn("[EK-TWEAK] sysctl_tcp_slow_start_idle restored: %d\n", orig_tcp_slow_start_idle); }

    pr_warn("[EK-TWEAK] UNLOADED cleanly.\n");
    return 0;
}

KPM_INIT(tweaker_init);
KPM_CTL0(tweaker_ctl0);
KPM_EXIT(tweaker_exit);

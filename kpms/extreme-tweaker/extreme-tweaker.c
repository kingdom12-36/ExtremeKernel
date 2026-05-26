/* SPDX-License-Identifier: GPL-2.0 */
/*
 * extreme-tweaker.kpm  v2.0  -  ExtremeKernel Network & Memory Battery Tweaker
 * Samsung Galaxy S20 Ultra (d2s, Exynos 9825), kernel 4.14
 *
 * All values are saved on load and fully restored on unload.
 * Has zero permanent effect — load to enable, unload to undo everything.
 *
 * Tweaks applied:
 *   vm_dirty_bytes / vm_dirty_background_bytes -> 0
 *       Samsung sets these non-zero which silently overrides and ignores the
 *       ratio-based dirty control below. Must be zeroed first.
 *   vm_dirty_ratio                -> 30%  (less frequent background flushing)
 *   vm_dirty_background_ratio     -> 8%   (background writeback threshold)
 *   sysctl_tcp_fastopen           -> 3    (TFO client+server: fewer round-trips
 *                                          = radio stays off sooner)
 *   sysctl_tcp_slow_start_after_idle -> 0 (no slow-start penalty when app
 *                                          resumes a connection: less radio-on time)
 *
 * Symbol names verified on-device via /proc/kallsyms on Samsung 4.14:
 *   vm_swappiness, vm_dirty_ratio, vm_dirty_background_ratio   confirmed
 *   vm_dirty_bytes, vm_dirty_background_bytes                  confirmed
 *   sysctl_tcp_fastopen                                        confirmed
 *   sysctl_tcp_slow_start_after_idle                           confirmed
 */

#include <compiler.h>
#include <kpmodule.h>
#include <kputils.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <kallsyms.h>

KPM_NAME("extreme-tweaker");
KPM_VERSION("2.0.0");
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

/* Saved originals — restored on exit */
static int            orig_dirty_ratio         = 20;
static int            orig_dirty_bg_ratio      = 10;
static unsigned long  orig_dirty_bytes         = 0;
static unsigned long  orig_dirty_bg_bytes      = 0;
static unsigned int   orig_tcp_fastopen        = 1;
static int            orig_tcp_slow_start_idle = 1;

/* Resolved at runtime to avoid unresolvable UND ELF symbol */
static int (*kpm_snprintf)(char *buf, size_t size, const char *fmt, ...) = 0;

static long tweaker_init(const char *args, const char *event, void *__user reserved)
{
    pr_info("[ek-tweaker] ExtremeKernel Network & Memory Battery Tweaker v2.0 loading...\n");

    kpm_snprintf = (typeof(kpm_snprintf))kallsyms_lookup_name("snprintf");

    p_dirty_ratio         = (int *)kallsyms_lookup_name("vm_dirty_ratio");
    p_dirty_bg_ratio      = (int *)kallsyms_lookup_name("vm_dirty_background_ratio");
    p_dirty_bytes         = (unsigned long *)kallsyms_lookup_name("vm_dirty_bytes");
    p_dirty_bg_bytes      = (unsigned long *)kallsyms_lookup_name("vm_dirty_background_bytes");
    p_tcp_fastopen        = (unsigned int *)kallsyms_lookup_name("sysctl_tcp_fastopen");
    p_tcp_slow_start_idle = (int *)kallsyms_lookup_name("sysctl_tcp_slow_start_after_idle");

    /*
     * Samsung ships vm_dirty_bytes / vm_dirty_background_bytes != 0.
     * When non-zero, the kernel ignores vm_dirty_ratio entirely.
     * Save originals then zero them so ratio-based control takes effect.
     */
    if (p_dirty_bytes) {
        orig_dirty_bytes = *p_dirty_bytes;
        if (orig_dirty_bytes != 0) {
            *p_dirty_bytes = 0;
            pr_info("[ek-tweaker] vm_dirty_bytes:              %lu -> 0 (unlocking ratio control)\n", orig_dirty_bytes);
        }
    } else {
        pr_warn("[ek-tweaker] vm_dirty_bytes not found\n");
    }
    if (p_dirty_bg_bytes) {
        orig_dirty_bg_bytes = *p_dirty_bg_bytes;
        if (orig_dirty_bg_bytes != 0) {
            *p_dirty_bg_bytes = 0;
            pr_info("[ek-tweaker] vm_dirty_background_bytes:   %lu -> 0 (unlocking ratio control)\n", orig_dirty_bg_bytes);
        }
    } else {
        pr_warn("[ek-tweaker] vm_dirty_background_bytes not found\n");
    }

    if (p_dirty_ratio) {
        orig_dirty_ratio = *p_dirty_ratio;
        *p_dirty_ratio = 30;
        pr_info("[ek-tweaker] vm_dirty_ratio:              %d -> 30%%\n", orig_dirty_ratio);
    } else {
        pr_err("[ek-tweaker] MISSING: vm_dirty_ratio\n");
    }
    if (p_dirty_bg_ratio) {
        orig_dirty_bg_ratio = *p_dirty_bg_ratio;
        *p_dirty_bg_ratio = 8;
        pr_info("[ek-tweaker] vm_dirty_background_ratio:   %d -> 8%%\n", orig_dirty_bg_ratio);
    } else {
        pr_err("[ek-tweaker] MISSING: vm_dirty_background_ratio\n");
    }
    if (p_tcp_fastopen) {
        orig_tcp_fastopen = *p_tcp_fastopen;
        *p_tcp_fastopen = 3;
        pr_info("[ek-tweaker] sysctl_tcp_fastopen:         %u -> 3\n", orig_tcp_fastopen);
    } else {
        pr_err("[ek-tweaker] MISSING: sysctl_tcp_fastopen\n");
    }
    if (p_tcp_slow_start_idle) {
        orig_tcp_slow_start_idle = *p_tcp_slow_start_idle;
        *p_tcp_slow_start_idle = 0;
        pr_info("[ek-tweaker] sysctl_tcp_slow_start_idle:  %d -> 0\n", orig_tcp_slow_start_idle);
    } else {
        pr_err("[ek-tweaker] MISSING: sysctl_tcp_slow_start_after_idle\n");
    }

    pr_info("[ek-tweaker] All tweaks applied. Unload to restore all originals.\n");
    return 0;
}

static long tweaker_ctl0(const char *args, char *__user out_msg, int outlen)
{
    char buf[640];
    int n = 0;
    if (kpm_snprintf) {
        n = kpm_snprintf(buf, sizeof(buf),
            "extreme-tweaker v2.0 — live values (orig):\n"
            "  vm_dirty_bytes             = %lu (was %lu)\n"
            "  vm_dirty_background_bytes  = %lu (was %lu)\n"
            "  vm_dirty_ratio             = %d%% (was %d%%)\n"
            "  vm_dirty_background_ratio  = %d%% (was %d%%)\n"
            "  sysctl_tcp_fastopen        = %u (was %u)\n"
            "  sysctl_tcp_slow_start      = %d (was %d)\n",
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
    pr_info("[ek-tweaker] Unloading — restoring ALL original values...\n");

    if (p_dirty_bytes)         { *p_dirty_bytes         = orig_dirty_bytes;         pr_info("[ek-tweaker] vm_dirty_bytes restored:             %lu\n", orig_dirty_bytes); }
    if (p_dirty_bg_bytes)      { *p_dirty_bg_bytes      = orig_dirty_bg_bytes;      pr_info("[ek-tweaker] vm_dirty_background_bytes restored:  %lu\n", orig_dirty_bg_bytes); }
    if (p_dirty_ratio)         { *p_dirty_ratio         = orig_dirty_ratio;         pr_info("[ek-tweaker] vm_dirty_ratio restored:             %d\n", orig_dirty_ratio); }
    if (p_dirty_bg_ratio)      { *p_dirty_bg_ratio      = orig_dirty_bg_ratio;      pr_info("[ek-tweaker] vm_dirty_background_ratio restored:  %d\n", orig_dirty_bg_ratio); }
    if (p_tcp_fastopen)        { *p_tcp_fastopen        = orig_tcp_fastopen;        pr_info("[ek-tweaker] sysctl_tcp_fastopen restored:         %u\n", orig_tcp_fastopen); }
    if (p_tcp_slow_start_idle) { *p_tcp_slow_start_idle = orig_tcp_slow_start_idle; pr_info("[ek-tweaker] sysctl_tcp_slow_start_idle restored:  %d\n", orig_tcp_slow_start_idle); }

    pr_info("[ek-tweaker] All values restored. Kernel back to original state.\n");
    return 0;
}

KPM_INIT(tweaker_init);
KPM_CTL0(tweaker_ctl0);
KPM_EXIT(tweaker_exit);

// SPDX-License-Identifier: GPL-2.0
/*
 * fs/susfs_ksu_bridge.c - KernelSU-Next compatibility bridge for SUSFS v2.1.0
 *
 * Implements the functions SUSFS v2.1.0 expects from KernelSU, plus the
 * prctl-based command dispatcher used by the userspace ksu_susfs tool.
 *
 * For KernelSU-Next (which does not natively ship SUSFS integration), we:
 *   - Implement susfs_is_current_ksu_domain() via a simple UID-0 check.
 *   - Forward susfs_is_allow_su() to KernelSU-Next's __ksu_is_allow_uid().
 *   - Implement ksu_escape_to_root() by committing full-root credentials.
 *   - Implement susfs_prctl_dispatch() called from kernel/sys.c prctl hook.
 */

#include <linux/cred.h>
#include <linux/uidgid.h>
#include <linux/capability.h>
#include <linux/susfs_def.h>
#include <linux/susfs.h>
#include <linux/uaccess.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/jump_label.h>

#ifdef CONFIG_KSU_SUSFS

/*
 * Global SuSFS state variables.
 *
 * susfs_is_avc_log_spoofing_enabled: toggled by susfs_set_avc_log_spoofing()
 *   and read via READ_ONCE() in security/selinux/hooks.c.
 * susfs_avc_log_spoofing_key_true: static branch used by avc_audit_post_callback()
 *   in security/selinux/avc.c.  Initialized FALSE (spoofing off by default).
 * susfs_ksu_sid / susfs_priv_app_sid: SELinux SIDs set by userspace policy
 *   loader; used by avc_audit_post_callback() when spoofing is active.
 */
bool susfs_is_avc_log_spoofing_enabled = false;
DEFINE_STATIC_KEY_FALSE(susfs_avc_log_spoofing_key_true);
u32 susfs_ksu_sid = 0;
u32 susfs_priv_app_sid = 0;

/*
 * try_umount() — unmount a kernel path.
 *
 * Called from susfs_try_umount() (fs/susfs.c) to unmount paths that were
 * registered via CMD_SUSFS_ADD_TRY_UMOUNT.  Uses kern_path() + path_umount()
 * so no userspace pointer is needed; MNT_DETACH is always OR-ed in to avoid
 * blocking when the mount is busy.
 */
#ifdef CONFIG_KSU_SUSFS_TRY_UMOUNT
extern int path_umount(struct path *path, int flags);
void try_umount(const char *mnt, int flags)
{
        struct path path;
        int err;

        err = kern_path(mnt, 0, &path);
        if (err) {
                pr_warn_ratelimited("susfs: try_umount kern_path('%s'): %d\n",
                                    mnt, err);
                return;
        }
        err = path_umount(&path, flags | MNT_DETACH);
        if (err)
                pr_warn_ratelimited("susfs: try_umount('%s'): %d\n", mnt, err);
}
#endif /* CONFIG_KSU_SUSFS_TRY_UMOUNT */

/*
 * susfs_is_current_ksu_domain() — true if the calling process is running as
 * KernelSU root (UID 0 via KSU credential grant).
 *
 * For KernelSU-Next every KSU-escalated process carries uid/euid == 0, which
 * is what SUSFS needs to decide whether to show real mounts / paths.
 */
bool susfs_is_current_ksu_domain(void)
{
        return uid_eq(current_uid(), GLOBAL_ROOT_UID);
}

/*
 * susfs_is_allow_su() — true if the calling process's UID is in the
 * KernelSU-Next allowlist.
 */
extern bool __ksu_is_allow_uid(uid_t uid);

bool susfs_is_allow_su(void)
{
        return unlikely(__ksu_is_allow_uid(current_uid().val));
}

/*
 * ksu_escape_to_root() — commit full root credentials to the calling task.
 * Called by sus_su after authenticating via the FIFO token.
 */
void ksu_escape_to_root(void)
{
        struct cred *cred;

        cred = prepare_creds();
        if (!cred)
                return;

        cred->uid   = cred->euid   = cred->suid   = cred->fsuid = GLOBAL_ROOT_UID;
        cred->gid   = cred->egid   = cred->sgid   = cred->fsgid = GLOBAL_ROOT_GID;
        cred->cap_inheritable = CAP_FULL_SET;
        cred->cap_permitted   = CAP_FULL_SET;
        cred->cap_effective   = CAP_FULL_SET;
        cred->cap_bset        = CAP_FULL_SET;
        cred->cap_ambient     = CAP_FULL_SET;

        commit_creds(cred);
}

/*
 * susfs_prctl_dispatch() — dispatch CMD_SUSFS_* commands from userspace.
 *
 * Called from kernel/sys.c when prctl(SUSFS_MAGIC, cmd, arg, ...) arrives.
 * Only UID-0 processes may issue these commands (enforced by the caller).
 */
void susfs_prctl_dispatch(unsigned long cmd, unsigned long __user *arg)
{
        void __user *user_arg = (void __user *)arg;

        switch (cmd) {
#ifdef CONFIG_KSU_SUSFS_SUS_PATH
        case CMD_SUSFS_ADD_SUS_PATH:
                susfs_add_sus_path(&user_arg);
                break;
        case CMD_SUSFS_ADD_SUS_PATH_LOOP:
                susfs_add_sus_path_loop(&user_arg);
                break;
#endif

#ifdef CONFIG_KSU_SUSFS_SUS_MOUNT
        case CMD_SUSFS_HIDE_SUS_MNTS_FOR_NON_SU_PROCS:
                susfs_set_hide_sus_mnts_for_non_su_procs(&user_arg);
                break;
#endif

#ifdef CONFIG_KSU_SUSFS_SUS_KSTAT
        case CMD_SUSFS_ADD_SUS_KSTAT:
        case CMD_SUSFS_ADD_SUS_KSTAT_STATICALLY:
                susfs_add_sus_kstat(&user_arg);
                break;
        case CMD_SUSFS_UPDATE_SUS_KSTAT:
                susfs_update_sus_kstat(&user_arg);
                break;
#endif

#ifdef CONFIG_KSU_SUSFS_TRY_UMOUNT
        case CMD_SUSFS_ADD_TRY_UMOUNT:
                susfs_add_try_umount(&user_arg);
                break;
#endif

#ifdef CONFIG_KSU_SUSFS_SPOOF_UNAME
        case CMD_SUSFS_SET_UNAME:
                susfs_set_uname(&user_arg);
                break;
#endif

#ifdef CONFIG_KSU_SUSFS_ENABLE_LOG
        case CMD_SUSFS_ENABLE_LOG:
                susfs_enable_log(&user_arg);
                break;
#endif

#ifdef CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG
        case CMD_SUSFS_SET_CMDLINE_OR_BOOTCONFIG:
                susfs_set_cmdline_or_bootconfig(&user_arg);
                break;
#endif

#ifdef CONFIG_KSU_SUSFS_OPEN_REDIRECT
        case CMD_SUSFS_ADD_OPEN_REDIRECT:
                susfs_add_open_redirect(&user_arg);
                break;
#endif

#ifdef CONFIG_KSU_SUSFS_SUS_MAP
        case CMD_SUSFS_ADD_SUS_MAP:
                susfs_add_sus_map(&user_arg);
                break;
#endif

        case CMD_SUSFS_ENABLE_AVC_LOG_SPOOFING:
                susfs_set_avc_log_spoofing(&user_arg);
                break;

        case CMD_SUSFS_SHOW_VERSION:
                susfs_show_version(&user_arg);
                break;

        case CMD_SUSFS_SHOW_ENABLED_FEATURES:
                susfs_get_enabled_features(&user_arg);
                break;

        case CMD_SUSFS_SHOW_VARIANT:
                susfs_show_variant(&user_arg);
                break;

        default:
                break;
        }
}

#endif /* CONFIG_KSU_SUSFS */

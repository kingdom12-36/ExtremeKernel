#ifndef __KSU_H_SUS_SU
#define __KSU_H_SUS_SU

/*
 * Bridge functions implemented in fs/susfs_ksu_bridge.c.
 * These replace the original drivers/kernelsu/core_hook.h dependency
 * and work with KernelSU-Next.
 */
extern bool susfs_is_allow_su(void);
extern void ksu_escape_to_root(void);

int sus_su_fifo_init(int *maj_dev_num, char *drv_path);
int sus_su_fifo_exit(int *maj_dev_num, char *drv_path);

#endif

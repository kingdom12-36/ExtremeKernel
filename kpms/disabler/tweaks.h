/* ================================================================
 * tweaks.h — ExtremeKernel Disabler: your config list
 * ================================================================
 *
 * HOW TO ADD A TWEAK:
 *   EK_DISABLE("kallsyms_symbol_name", value)
 *
 * HOW TO FIND A SYMBOL NAME (run in Termux):
 *   su -c "cat /proc/kallsyms | grep -i panic_on_oops"
 *   The second column is the symbol name.
 *
 * HOW TO REMOVE A TWEAK:
 *   Comment it out with // or delete the line.
 *
 * IMPORTANT: Only int-sized kernel variables go here.
 *            For unsigned long vars, see the SPECIAL CASES
 *            section at the bottom of disabler.c.
 *
 * After editing → rebuild via build-kpm.yml → reinstall .kpm
 * ================================================================ */

/* --- Panic / crash behaviour ---
 * If "MISSING" shows in dmesg for any of these, the Samsung 4.14 kernel
 * does not export that symbol. Check the exact name with:
 *   su -c "cat /proc/kallsyms | grep -i panic_on_oops"
 */
EK_DISABLE("panic_on_oops",    0)   /* don't reboot on kernel oops, just log */
EK_DISABLE("softlockup_panic", 0)   /* don't reboot on soft lockup */
EK_DISABLE("hardlockup_panic", 0)   /* don't reboot on hard lockup */

/* --- Scheduler debug overhead --- */
EK_DISABLE("sched_schedstats", 0)   /* disable sched statistics collection */

/* --- console_loglevel ---
 * Intentionally REMOVED from defaults.
 * On Samsung 4.14 the symbol may not be in kallsyms; lowering it also
 * suppresses the pr_warn dmesg messages that confirm modules loaded.
 * Add it back only if you specifically need to silence the console:
 *   EK_DISABLE("console_loglevel", 3)
 */

/* --- Add your own below --- */
/* EK_DISABLE("some_symbol", 0) */

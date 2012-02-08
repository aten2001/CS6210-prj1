/*
 * gt_signal.h
 *
 */

#ifndef GT_SIGNAL_H_
#define GT_SIGNAL_H_

/**********************************************************************/
/* kthread signal handling */
extern void sig_install_handler_and_unblock(int signo, void (*handler)(int));
extern void sig_block_signal(int signo);
extern void sig_unblock_signal(int signo);

#define KTHREAD_VTALRM_SEC 0
#define KTHREAD_VTALRM_USEC 100000
extern void sig_init_vtalrm_timeslice();

#endif /* GT_SIGNAL_H_ */

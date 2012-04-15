/*
 * gt_scheduler.c
 *
 *  Created on: Feb 3, 2012
 *      Author: sam
 */

#include <setjmp.h>
#include <unistd.h>

#include "gt_scheduler.h"
#include "gt_kthread.h"
#include "gt_uthread.h"
#include "gt_uthread.h"
#include "gt_spinlock.h"
#include "gt_common.h"
#include "gt_signal.h"

scheduler_t scheduler;

/*
 * Preempts, schedules, and dispatches a new uthread
 */
void schedule(kthread_t *old_k_ctx)
{
	checkpoint("k%d: scheduling", old_k_ctx->cpuid);
	uthread_t *cur_uthread = scheduler.preempt_current_uthread(old_k_ctx);
	if (cur_uthread != NULL){
	  if(cur_uthread->state != UTHREAD_DONE) {
		checkpoint("u%d: Calling setjmp", cur_uthread->tid);
		if (sigsetjmp(cur_uthread->env, 1)) {
			sig_install_handler_and_unblock(SIGSCHED,
			                                &kthread_sched_handler);
			return; // resume uthread
		}
	  }
	}

	kthread_t *k_ctx = kthread_current_kthread();
	uthread_t *next_uthread = scheduler.pick_next_uthread(k_ctx);
	if (next_uthread == NULL) {
		/* we're done with all our uhreads */
		checkpoint("k%d: NULL next_uthread", k_ctx->cpuid);
		checkpoint("k%d: Setting state to DONE", k_ctx->cpuid);
		k_ctx->state = KTHREAD_DONE;
		checkpoint("k%d: Calling longjmp", k_ctx->cpuid);
		siglongjmp(k_ctx->env, 1);
		return; // ? exit kthread? wait for more uthreads?
	}

	/* note: current_uthread must be set before calling uthread_init() */
	k_ctx->current_uthread = next_uthread;
	int first_time = 0;
	if (next_uthread->state == UTHREAD_INIT)
		first_time = 1;

	if (first_time && uthread_init(next_uthread))
		fail("uthread init");

	scheduler.resume_uthread(k_ctx); // possibly sets timer

	checkpoint("k%d: u%d: longjumping to uthread",
	           k_ctx->cpuid, next_uthread->tid);

	if (first_time) {
		uthread_context_func(0);
	} else {
		checkpoint("k%d: Calling longjmp", next_uthread->tid);
		siglongjmp(next_uthread->env, 1); // jump to above sigsetjump
	}
	return;
}

void scheduler_switch(scheduler_t *s, scheduler_type_t t, int lwp_count);
void scheduler_init(scheduler_t *scheduler, scheduler_type_t sched_type,
                    int lwp_count)
{
	gt_spinlock_init(&scheduler->lock);
	scheduler_switch(scheduler, sched_type, lwp_count);
	return;
}

void scheduler_destroy(scheduler_t *scheduler) {
	scheduler->data.destroy(scheduler->data.buf);
	return;
}

/*
 * gt_uthread.c
 *
 *  Created on: Feb 3, 2012
 *      Author: sam
 */
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <assert.h>
#include <sched.h>
#include <setjmp.h>
#include <string.h>

#include "gt_uthread.h"
#include "gt_kthread.h"
#include "gt_thread.h"
#include "gt_spinlock.h"
#include "gt_common.h"
#include "gt_scheduler.h"
#include "gt_signal.h"

#define UTHREAD_DEFAULT_SSIZE (16 * 1024 )
#define UTHREAD_SIGSTACK SIGUSR2

/* global scheduler */
extern scheduler_t scheduler;

gt_spinlock_t uthread_count_lock = GT_SPINLOCK_INITIALIZER;
int uthread_count = 0;

gt_spinlock_t uthread_init_lock = GT_SPINLOCK_INITIALIZER;

/* Serves as the launching off point and landing point for the user's uthread
 * execution.
 *
 * Does the initial setjmp so, on the first schedule, the user's
 * function is called (afterwards, execution is resumed by returning from signal
 * handlers). Also, uthreads end up here when the finish the user's function;
 * so this function launches back into the schedule() to schedule the next
 * available uthread.
 */
static void uthread_context_func(int signo)
{
	kthread_t *kthread = kthread_current_kthread();
	uthread_t *uthread = kthread->current_uthread;
	assert(kthread != NULL);
	assert(uthread != NULL);
	checkpoint("k%d: u%d: uthread_context_func .....",
	           kthread->cpuid, uthread->tid);
	/* kthread->cur_uthread points to newly created uthread */
	if (!sigsetjmp(uthread->env, 1)) {
		/* In UTHREAD_INIT : saves the context and returns.
		 * Otherwise, continues execution. */
		/* DONT USE any locks here !! */
		checkpoint("u%d: setting initial context", uthread->tid);

		assert(uthread->state == UTHREAD_INIT);
		uthread->state = UTHREAD_RUNNABLE;
		return;
	}

	/* UTHREAD_RUNNING : siglongjmp was executed. */
	sig_install_handler_and_unblock(SIGSCHED, &kthread_sched_handler);
	assert(uthread->state == UTHREAD_RUNNING);
	checkpoint("u%d: longjmp was executed, starting task for first time",
	           uthread->tid);

	/* Execute the new_uthread task */
	while(gettimeofday(&uthread->attr->timeslice_start, NULL));
	checkpoint("u%d: Start time: %ld.%06ld s", uthread->tid,
	           uthread->attr->timeslice_start.tv_sec,
	           uthread->attr->timeslice_start.tv_usec);
	uthread->start_routine(uthread->arg);
	checkpoint("u%d: getting final elapsed time", uthread->tid);
	uthread_attr_set_elapsed_cpu_time(uthread->attr);
	uthread->state = UTHREAD_DONE;

	checkpoint("u%d: task ended normally", uthread->tid);

//	kill(getpid(), SIGSCHED);

	/* schedule the next thread, if there is one */
	schedule(kthread);


	checkpoint("%s", "exiting context fcn");
	return;
}

/* Initializes uthread. Sets up a stack through the use of SIGUSR2. Must be
 * called *after* the kthread it is going to be scheduled on has been
 * initialized  */
int uthread_init(uthread_t *uthread)
{
	checkpoint("u%d: Initializing uthread...", uthread->tid);
	stack_t oldstack;
	sigset_t set, oldset;
	struct sigaction act, oldact;

	gt_spin_lock(&uthread_init_lock);

	/* Allocate new stack for uthread */
	uthread->stack.ss_flags = 0; /* Stack enabled for signal handling */
	uthread->stack.ss_size = UTHREAD_DEFAULT_SSIZE;
	uthread->stack.ss_sp = emalloc(uthread->stack.ss_size);

	/* Register a signal(SIGSTACK) for alternate stack */
	memset(&act, 0, sizeof(act));
	act.sa_handler = &uthread_context_func;
	act.sa_flags = (SA_ONSTACK | SA_RESTART);
	if (sigaction(UTHREAD_SIGSTACK, &act, &oldact))
		fail_perror("uthread SIGSTACK handler install");

	/* Install alternate signal stack (for SIGSTACK) */
	checkpoint("u%d: Installing alternate stack", uthread->tid);
	if (sigaltstack(&(uthread->stack), &oldstack))
		fail_perror("uthread sigaltstack install");

	/* Unblock the signal(SIGSTACK) */
	sigemptyset(&set);
	sigaddset(&set, UTHREAD_SIGSTACK);
	sigprocmask(SIG_UNBLOCK, &set, &oldset);

	/* Note that the SIGSTACK handler expects kthread->current_uthread
	 * to point to this newly created thread. This implies that this
	 * must be called from within a running kthread */
	checkpoint("u%d: raising SIGSTACK", uthread->tid);
	if (kill(getpid(), UTHREAD_SIGSTACK))
		fail_perror("kill");
	assert(uthread != NULL);
	assert(uthread->state == UTHREAD_RUNNABLE);
	checkpoint("u%d: returned from SIGSTACK", uthread->tid);

	/* Block the signal(SIGSTACK) */
	sigemptyset(&set);
	sigaddset(&set, UTHREAD_SIGSTACK);
	sigprocmask(SIG_BLOCK, &set, &oldset);
	if (sigaction(UTHREAD_SIGSTACK, &oldact, NULL))
		fail_perror("uthread SIGSTACK revert");

	/* Disable the stack for signal(SIGSTACK) handling */
	uthread->stack.ss_flags = SS_DISABLE;

	/* Restore the old stack/signal handling */
	if (sigaltstack(&oldstack, NULL))
		fail_perror("uthread sigaltstack revert");

	gt_spin_unlock(&uthread_init_lock);
	checkpoint("u%d: initialized", uthread->tid);
	return 0;
}


int uthread_create(uthread_tid *u_tid, uthread_attr_t *attr,
                   int(*start_routine)(void *),
                   void *arg)
{
	if (attr == NULL) {
		attr = uthread_attr_create();
	}

	checkpoint("%s", "Creating uthread...");
	uthread_t *new_uthread = ecalloc(sizeof(*new_uthread));
	new_uthread->state = UTHREAD_INIT;
	new_uthread->start_routine = start_routine;
	new_uthread->arg = arg;
	new_uthread->attr = attr;

	gt_spin_lock(&uthread_count_lock);
	new_uthread->tid = uthread_count++;
	gt_spin_unlock(&uthread_count_lock);
	*u_tid = new_uthread->tid;

//	uthread_init(new_uthread); // init from running kthread
	checkpoint("%s", "sending to scheduler");
	kthread_t *kthread = scheduler.uthread_init(new_uthread);
	checkpoint("%s", "scheduler init returned");
	assert(kthread != NULL);
	if (kthread->state != KTHREAD_RUNNING) {
		/* our kthread is waiting for its first uthread. wake it up */
		checkpoint("k%d: Sending SIGSCHED", kthread->cpuid);
		kill(kthread->tid, SIGSCHED);
		sched_yield();
	}
	checkpoint("u%d: created", new_uthread->tid);
	return 0;
}

/* Suspends the currently running uthread and causes the next to be scheduled */
void uthread_yield()
{
	checkpoint("k%d: u%d: Yielding",
	           (kthread_current_kthread())->cpuid,
	           (kthread_current_kthread())->current_uthread->tid);
	kill(getpid(), SIGSCHED);
}

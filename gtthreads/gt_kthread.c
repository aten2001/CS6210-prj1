/*
 * gt_kthread.c
 *
 *  Created on: Feb 3, 2012
 *      Author: sam
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <signal.h>
#include <sched.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>

#include "gt_kthread.h"
#include "gt_uthread.h"
#include "gt_common.h"
#include "gt_scheduler.h"
#include "gt_signal.h"

#define KTHREAD_DEFAULT_SSIZE (256 * 1024)
#define KTHREAD_MAX_COUNT 16

extern scheduler_t scheduler;

kthread_t *_kthread_cpu_map[KTHREAD_MAX_COUNT];
gt_spinlock_t cpu_map_lock = GT_SPINLOCK_INITIALIZER;

/* returns the currently running kthread */
kthread_t *kthread_current_kthread()
{
	gt_spin_lock(&cpu_map_lock);
	kthread_t *k_ctx = _kthread_cpu_map[kthread_apic_id()];
	gt_spin_unlock(&cpu_map_lock);
	assert(k_ctx != NULL);
	return k_ctx;
}

/* sig handler to set the flag so we know we can exit */
void kthread_exit_handler(int signo) {
	kthread_t *k_ctx = kthread_current_kthread();
	checkpoint("k%d: Exit handler", k_ctx->cpuid);
	k_ctx->can_exit = 1;
}

/* returns 1 if a kthread is schedulable, 0 otherwise */
int kthread_is_schedulable(kthread_t *k_ctx) {
	return (!(k_ctx == NULL ||
		  k_ctx->state == KTHREAD_INIT )); //||
//		  k_ctx->state == KTHREAD_DONE));
}

extern int kthread_count;
extern gt_spinlock_t kthread_count_lock;
static void kthread_exit(kthread_t *k_ctx)
{
	checkpoint("k%d: exiting", k_ctx->cpuid);
	gt_spin_lock(&kthread_count_lock);
	kthread_count--;
	gt_spin_unlock(&kthread_count_lock);
	free(k_ctx);
	exit(EXIT_SUCCESS);
}

static void kthread_wait_for_uthread(kthread_t *k_ctx);
/* signal handler for SIGSCHED. */
void kthread_sched_handler(int signo)
{
	checkpoint("%s", "***Entering signal handler***");
	kthread_t *k_ctx = kthread_current_kthread();
	if (k_ctx->current_uthread)
		uthread_attr_set_elapsed_cpu_time(k_ctx->current_uthread->attr);
	schedule(k_ctx);
	k_ctx = kthread_current_kthread(); // not sure if i trust my stack; call again
	if (k_ctx->state != KTHREAD_RUNNING) {
		checkpoint("k%d: no uthread to run", k_ctx->cpuid);
		checkpoint("k%d: exiting handler", k_ctx->cpuid);
//		return;
		kthread_wait_for_uthread(k_ctx);
	}
	gettimeofday(&k_ctx->current_uthread->attr->timeslice_start, NULL);
//	sig_install_handler_and_unblock(SIGSCHED, &kthread_sched_handler);
	checkpoint("k%d: exiting handler", k_ctx->cpuid);
}

/* blocks until there is a uthread to schedule */
static void kthread_wait_for_uthread(kthread_t *k_ctx)
{
	sigset_t mask, oldmask;
	memset(&mask, 0, sizeof(mask));
	memset(&oldmask, 0, sizeof(oldmask));
	sigemptyset(&mask);
	sigaddset(&mask, SIGSCHED);
	sigaddset(&mask, SIGUSR1);
	sigprocmask(SIG_BLOCK, &mask, &oldmask);
	struct timespec timeout = {0, 500000000};
	int sig;
	while (!(k_ctx->state == KTHREAD_DONE && k_ctx->can_exit)) {
		checkpoint("State is %d and can_exit is %d",
		           k_ctx->state, k_ctx->can_exit);
//		k_ctx->state = KTHREAD_RUNNABLE;
		if (sigsetjmp(k_ctx->env, 1)) {
			checkpoint("%s", "uthreads done");
			assert(k_ctx->state == KTHREAD_DONE);
			continue; // get here when all uthreads are finished
		}

		checkpoint("k%d: kthread (%d) waiting for first uthread",
		           k_ctx->cpuid, k_ctx->tid);
		sig = sigtimedwait(&mask, NULL, &timeout);
		if (sig == SIGSCHED) {
			checkpoint("k%d: kthread (%d) received first uthread",
				   k_ctx->cpuid, k_ctx->tid);
			k_ctx->state = KTHREAD_RUNNING;
			kthread_sched_handler(sig);
			checkpoint("k%d: returning from sched handler in wait(), state is %d",
			           k_ctx->cpuid, k_ctx->state);
		} else if (sig == SIGUSR1) {
			kthread_exit_handler(sig);
		}
	}
	checkpoint("k%d: exiting wait for uthread", k_ctx->cpuid);
	kthread_exit(k_ctx);
}

/* main function is to set the cpu affinity */
static void kthread_init(kthread_t *k_ctx)
{
	k_ctx->pid = getpid();
	k_ctx->tid = syscall(SYS_gettid);
	assert(k_ctx->pid == k_ctx->tid);

	cpu_set_t cpu_affinity_mask;
	CPU_ZERO(&cpu_affinity_mask);
	CPU_SET(k_ctx->cpuid, &cpu_affinity_mask);
	sched_setaffinity(k_ctx->tid, sizeof(cpu_affinity_mask),
	                  &cpu_affinity_mask);

	sched_yield(); /* gets us on our target cpu */

	k_ctx->cpu_apic_id = kthread_apic_id();
	_kthread_cpu_map[k_ctx->cpu_apic_id] = k_ctx;
	return;
}

static int kthread_start(void *arg)
{
	kthread_t *k_ctx = arg;
	kthread_init(k_ctx);
	scheduler.kthread_init(k_ctx);
	k_ctx->state = KTHREAD_RUNNABLE;
	sig_install_handler_and_unblock(SIGUSR1, &kthread_exit_handler);
	kthread_wait_for_uthread(k_ctx);
	return 0;
}

/* kthread creation. Returns a pointer to the kthread_t if successful, NULL
 * otherwise */
kthread_t *kthread_create(pid_t *tid, int lwp)
{
	/* Create the new thread's stack */
	size_t stacksize = KTHREAD_DEFAULT_SSIZE;
	char *stack = ecalloc(stacksize);
	stack += stacksize;  // grows down

	/* set up the context */
	kthread_t *k_ctx = ecalloc(sizeof(*k_ctx));
	k_ctx->cpuid = lwp;
	k_ctx->state = KTHREAD_INIT;

	/* block SIGSCHED before cloning. This way no kthread will try to
	 * schedule a uthread before it's ready. But note that we don't set
	 * CLONE_SIGHAND in `flags`---so changes to signal handlers are not
	 * shared after the clone.
	 */
	sig_block_signal(SIGSCHED);

	/* block SIGUSR1 (the exit signal) for the same reason */
	sig_block_signal(SIGUSR1);

	int flags = CLONE_VM | CLONE_FS | CLONE_FILES | SIGCHLD;
	*tid = clone(kthread_start, (void *) stack, flags, (void *) k_ctx);
	if (*tid < 0) {
		perror("clone");
		return NULL;
	}
	return k_ctx;
}

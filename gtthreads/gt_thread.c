/*
 * gt_thread.c
 *
 *  Created on: Feb 3, 2012
 *      Author: sam
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sched.h>
#include <errno.h>

#include "gt_thread.h"
#include "gt_kthread.h"
#include "gt_uthread.h"
#include "gt_common.h"
#include "gt_spinlock.h"
#include "gt_scheduler.h"
#include "gt_signal.h"

/* global singleton scheduler */
extern scheduler_t scheduler;

/* for thread-safe malloc */
gt_spinlock_t MALLOC_LOCK = GT_SPINLOCK_INITIALIZER;

/* used for keeping track of when to quit. kthreads decrement on exit */
int kthread_count = 0;
gt_spinlock_t kthread_count_lock = GT_SPINLOCK_INITIALIZER;

/* The next several functions and data handle synchronizing with the child
 * kthreads after they are done initializing */

/* linked list for keeping up with the kthreads created */
typedef struct kthread_node {
	kthread_t *k_ctx;
	struct kthread_node *next;
} kthread_node_t;

kthread_node_t *kthread_list = NULL;

static void append_kthread(kthread_t *k_ctx) {
	kthread_node_t *node = emalloc(sizeof(*node));
	node->k_ctx = k_ctx;
	node->next = kthread_list;
	kthread_list = node;
}

static int list_is_done(kthread_node_t *p)
{
	while (p != NULL) {
		if (!kthread_is_schedulable(p->k_ctx))
			return 0;
		p = p->next;
	}
	return 1;
}

static void wait_for_kthread_init_completion()
{
	checkpoint("%s", "Waiting for kthreads to init");
	int done = 0;
	while (!done) {
		sched_yield();
		done = list_is_done(kthread_list);
	}
	checkpoint("%s", "All kthreads ready");
}

void gtthread_options_init(gtthread_options_t *options)
{
	options->scheduler_type = SCHEDULER_DEFAULT;
	options->lwp_count = 0;
}

static void _gtthread_app_init(gtthread_options_t *options);
void gtthread_app_init(gtthread_options_t *options)
{
	checkpoint("%s", "Entering app_init");
	/* Wrap the actual gtthread_app_init, initializing the options first
	 * if necessary */
	int free_opt = 0;
	if ((free_opt = (options == NULL))) {
		options = malloc(sizeof(*options));
		gtthread_options_init(options);
	}

	_gtthread_app_init(options);

	if (free_opt) {
		free(options);
	}
	checkpoint("%s", "Exiting app_init");
}

static void _gtthread_app_init(gtthread_options_t *options)
{
	/* Num of logical processors (cpus/cores) */
	if (options->lwp_count < 1) {
		options->lwp_count = (int) sysconf(_SC_NPROCESSORS_CONF);
	}
	scheduler_init(&scheduler, options->scheduler_type, options->lwp_count);

	pid_t k_tid;
	kthread_t *k_thread;
	for (int lwp = 0; lwp < options->lwp_count; lwp++) {
		if (!(k_thread = kthread_create(&k_tid, lwp)))
			fail_perror("kthread_create");
		append_kthread(k_thread);
		gt_spin_lock(&kthread_count_lock);
		kthread_count++;
		gt_spin_unlock(&kthread_count_lock);
		checkpoint("k%d: created", lwp);
	}
	wait_for_kthread_init_completion();
}

void wakeup(int signo)
{
	;
}

void gtthread_app_exit()
{
	checkpoint("%s", "\n\n\nEntering app_exit");
	/* first we signal to the kthreads that it is OK to exit. At this point,
	 * they shouldn't need to wait for any more uthreads to be created */
	kthread_node_t *p = kthread_list;
	while (p != NULL) {
		kill(p->k_ctx->pid, SIGUSR1);
		p = p->next;
	}

	sched_yield();

	/* be sure to wake from sleep */
	sig_install_handler_and_unblock(SIGCHLD, wakeup);

	gt_spin_lock(&kthread_count_lock);
	while (kthread_count) {
		checkpoint("%s", "Waiting for children");
		gt_spin_unlock(&kthread_count_lock);
		sleep(5);
		gt_spin_lock(&kthread_count_lock);
	}
	gt_spin_unlock(&kthread_count_lock);

	scheduler_destroy(&scheduler);
	checkpoint("%s", "Exiting app");
}

void gt_yield()
{
	uthread_yield();
}

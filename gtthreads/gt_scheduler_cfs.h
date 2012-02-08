/*
 * gt_cfs.h
 *
 * Implements the Completely Fair Scheduler, following the generic scheduling
 * interface
 *
 *  Created on: Feb 4, 2012
 *      Author: sam
 */

#ifndef GT_CFS_H_
#define GT_CFS_H_

#include "rb_tree/red_black_tree.h"

struct scheduler;
struct kthread;
struct uthread;

void cfs_init(struct scheduler *scheduler, int lwp_count);

#endif /* GT_CFS_H_ */

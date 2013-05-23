/*
 * Copyright (c) 2013 Richard Braun.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Lockless synchronization.
 */

#ifndef _KERN_LLSYNC_H
#define _KERN_LLSYNC_H

#include <kern/list.h>
#include <kern/macros.h>
#include <kern/llsync_i.h>
#include <kern/thread.h>
#include <machine/mb.h>

struct llsync_work;

/*
 * Type for work functions.
 *
 * Works are guaranteed to be processed in thread context and can block, but
 * must not sleep for long durations.
 */
typedef void (*llsync_fn_t)(struct llsync_work *);

/*
 * Deferred work.
 *
 * This structure should be embedded in objects protected with lockless
 * synchronization. It stores the work function and is passed to it as its
 * only parameter. The function can then find the containing object with
 * structof and release it.
 */
struct llsync_work {
    struct list node;
    llsync_fn_t fn;
};

/*
 * Safely assign a pointer.
 *
 * This macro enforces memory ordering. It should be used to reference
 * objects once they're completely built, so that readers accessing the
 * pointer obtain consistent data.
 */
#define llsync_assign_ptr(ptr, value)   \
MACRO_BEGIN                             \
    mb_store();                         \
    (ptr) = (value);                    \
MACRO_END

/*
 * Safely access a pointer.
 *
 * No memory barrier, rely on data dependency to enforce ordering.
 */
#define llsync_read_ptr(ptr) (ptr)

static inline void
llsync_read_lock(void)
{
    thread_preempt_disable();
}

static inline void
llsync_read_unlock(void)
{
    thread_preempt_enable();
}

/*
 * Reset the checkpoint flag of a processor.
 *
 * Called from interrupt context.
 */
static inline void
llsync_reset_checkpoint(unsigned int cpu)
{
    llsync_cpus[cpu].checked = 0;
}

/*
 * Report that a processor has reached a checkpoint.
 *
 * Called during context switch.
 */
static inline void
llsync_checkin(unsigned int cpu)
{
    llsync_cpus[cpu].checked = 1;
}

/*
 * Initialize the llsync module.
 */
void llsync_setup(void);

/*
 * Report that a processor will be regularly checking in.
 */
void llsync_register_cpu(unsigned int cpu);

/*
 * Report that a processor has entered a state in which checking in becomes
 * irrelevant (e.g. the idle loop).
 */
void llsync_unregister_cpu(unsigned int cpu);

/*
 * Commit a pending checkpoint.
 *
 * Checking in is a light processor-local operation. Committing a checkpoint
 * is a heavier global one, and is performed less often, normally during the
 * system timer interrupt.
 */
void llsync_commit_checkpoint(unsigned int cpu);

/*
 * Defer an operation until all existing read-side references are dropped,
 * without blocking.
 */
void llsync_defer(struct llsync_work *work, llsync_fn_t fn);

/*
 * Wait for all existing read-side references to be dropped.
 *
 * This function sleeps, and may do so for a moderately long duration (a few
 * system timer ticks).
 */
void llsync_wait(void);

#endif /* _KERN_LLSYNC_H */

/*
 * Copyright (c) 2012, 2013 Richard Braun.
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
 * The scheduling algorithm implemented by this module, named Distributed
 * Group Ratio Round-Robin (DGR3), is based on the following papers :
 *  - "Group Ratio Round-Robin: O(1) Proportional Share Scheduling for
 *    Uniprocessor and Multiprocessor Systems" by Bogdan Caprita, Wong Chun
 *    Chan, Jason Nieh, Clifford Stein and Haoqiang Zheng.
 *  - "Efficient and Scalable Multiprocessor Fair Scheduling Using Distributed
 *    Weighted Round-Robin" by Tong li, Dan Baumberger and Scott Hahn.
 *
 * Note that the Group Ratio Round-Robin (GR3) paper offers a multiprocessor
 * extension, but based on a single global queue, which strongly limits its
 * scalability on systems with many processors. That extension isn't used in
 * this implementation.
 *
 * The basic idea is to use GR3 for processor-local scheduling, and Distributed
 * Weighted Round-Robin (DWRR) for inter-processor load balancing. These
 * algorithms were chosen for several reasons. To begin with, they provide
 * fair scheduling, a very desirable property for a modern scheduler. Next,
 * being based on round-robin, their algorithmic complexity is very low (GR3
 * has O(1) scheduling complexity, and O(g) complexity on thread addition
 * or removal, g being the number of groups, with one group per priority, a
 * low number in practice). Finally, they're very simple to implement, making
 * them easy to adjust and maintain.
 *
 * Both algorithms are actually slightly modified for efficiency. First, this
 * version of GR3 is simplified by mapping exactly one group to one priority,
 * and in turn, one weight. This is possible because priorities are intended
 * to match Unix nice values, and systems commonly provide a constant, small
 * set of nice values. This removes the need for accounting deficit. Next,
 * round tracking is used to improve the handling of dynamic events : work
 * scaling is performed only on thread addition, and not when a thread that
 * was removed is added again during the same round. In addition, since GR3
 * is itself a round-robin algorithm, it already provides the feature required
 * from local scheduling by DWRR, namely round slicing. Consequently, DWRR
 * doesn't sit "on top" of GR3, but is actually merged with it. The result is
 * an algorithm that shares the same data for both local scheduling and load
 * balancing.
 *
 * A few terms are used by both papers with slightly different meanings. Here
 * are the definitions used in this implementation :
 *  - The time unit is the system timer period (1 / HZ)
 *  - Work is the amount of execution time units consumed
 *  - Weight is the amount of execution time units allocated
 *  - A round is the shortest period during which all threads in a run queue
 *    consume their allocated time (i.e. their work reaches their weight)
 *
 * TODO Sub-tick accounting.
 *
 * TODO CPU affinity.
 *
 * TODO Take into account the underlying CPU topology (and adjust load
 * balancing to access the global highest round less frequently on large
 * processor groups, perhaps by applying the load balancing algorithm in a
 * bottom-up fashion with one highest round per processor group).
 *
 * TODO For now, interactivity can not be experimented. The current strategy
 * is to always add threads in front of their group queue and track rounds
 * so that they don't get more time than they should. A direct consequence
 * is that continually spawning threads at short intervals is likely to cause
 * starvation. This could be fixed by adding newly created threads at the back
 * of their group queue. For now, don't overengineer, and wait until all this
 * can actually be tested.
 *
 * TODO Review weight computation (it may be more appropriate to determine
 * weights in a smoother way than a raw scaling).
 */

#include <kern/assert.h>
#include <kern/bitmap.h>
#include <kern/condition.h>
#include <kern/error.h>
#include <kern/init.h>
#include <kern/kmem.h>
#include <kern/list.h>
#include <kern/macros.h>
#include <kern/mutex.h>
#include <kern/panic.h>
#include <kern/param.h>
#include <kern/spinlock.h>
#include <kern/sprintf.h>
#include <kern/stddef.h>
#include <kern/string.h>
#include <kern/task.h>
#include <kern/thread.h>
#include <machine/atomic.h>
#include <machine/cpu.h>
#include <machine/mb.h>
#include <machine/pmap.h>
#include <machine/tcb.h>
#include <vm/vm_map.h>

/*
 * Default time slice for real-time round-robin scheduling.
 */
#define THREAD_DEFAULT_RR_TIME_SLICE (HZ / 10)

/*
 * Maximum number of threads which can be pulled from a remote run queue
 * while interrupts are disabled.
 */
#define THREAD_MAX_MIGRATIONS 16

/*
 * Delay (in ticks) between two balance attempts when a run queue is idle.
 */
#define THREAD_IDLE_BALANCE_TICKS (HZ / 2)

/*
 * Run queue properties for real-time threads.
 */
struct thread_rt_runq {
    unsigned int bitmap;
    struct list threads[THREAD_SCHED_RT_PRIO_MAX + 1];
};

/*
 * Initial value of the highest round.
 *
 * Set to a high value to make sure overflows are correctly handled.
 */
#define THREAD_TS_INITIAL_ROUND ((unsigned long)-10)

/*
 * Round slice base unit for time-sharing threads.
 */
#define THREAD_TS_ROUND_SLICE_BASE (HZ / 10)

/*
 * Group of threads sharing the same weight.
 */
struct thread_ts_group {
    struct list node;
    struct list threads;
    unsigned int weight;
    unsigned int work;
};

/*
 * Run queue properties for time-sharing threads.
 *
 * The current group pointer has a valid address only when the run queue isn't
 * empty.
 */
struct thread_ts_runq {
    struct thread_ts_group group_array[THREAD_SCHED_TS_PRIO_MAX + 1];
    struct list groups;
    struct list threads;
    struct thread_ts_group *current;
    unsigned int nr_threads;
    unsigned int weight;
    unsigned int work;
};

/*
 * Per processor run queue.
 *
 * Locking multiple run queues is done in the ascending order of their
 * addresses. Interrupts must be disabled whenever locking a run queue, even
 * a remote one, otherwise an interrupt (which invokes the scheduler on its
 * return path) may violate the locking order.
 */
struct thread_runq {
    struct spinlock lock;
    struct thread *current;
    unsigned int nr_threads;

    /* Real-time related members */
    struct thread_rt_runq rt_runq;

    /*
     * Time-sharing related members.
     *
     * The current round is set when the active run queue becomes non-empty.
     * It's not reset when both run queues become empty. As a result, the
     * current round has a meaningful value only when at least one thread is
     * present, i.e. the global weight isn't zero.
     */
    unsigned long ts_round;
    unsigned int ts_weight;
    struct thread_ts_runq ts_runqs[2];
    struct thread_ts_runq *ts_runq_active;
    struct thread_ts_runq *ts_runq_expired;

    struct thread *balancer;
    struct thread *idler;

    /* Ticks before the next balancing attempt when a run queue is idle */
    unsigned int idle_balance_ticks;
} __aligned(CPU_L1_SIZE);

/*
 * Operations of a scheduling class.
 */
struct thread_sched_ops {
    void (*init_thread)(struct thread *thread, unsigned short priority);
    struct thread_runq * (*select_runq)(void);
    void (*add)(struct thread_runq *runq, struct thread *thread);
    void (*remove)(struct thread_runq *runq, struct thread *thread);
    void (*put_prev)(struct thread_runq *runq, struct thread *thread);
    struct thread * (*get_next)(struct thread_runq *runq);
    void (*tick)(struct thread_runq *runq, struct thread *thread);
};

static struct thread_runq thread_runqs[MAX_CPUS];

/*
 * Statically allocated fake threads that provide thread context to processors
 * during bootstrap.
 */
static struct thread thread_booters[MAX_CPUS] __initdata;

/*
 * Caches for allocated threads and their stacks.
 */
static struct kmem_cache thread_cache;
static struct kmem_cache thread_stack_cache;

/*
 * Table used to quickly map policies to classes.
 */
static unsigned char thread_policy_table[THREAD_NR_SCHED_POLICIES];

/*
 * Scheduling class operations.
 */
static struct thread_sched_ops thread_sched_ops[THREAD_NR_SCHED_CLASSES];

static struct thread_attr thread_default_attr = {
    NULL,
    NULL,
    THREAD_SCHED_POLICY_TS,
    THREAD_SCHED_TS_PRIO_DEFAULT
};

BITMAP_DECLARE(thread_active_runqs, MAX_CPUS);

/*
 * System-wide value of the current highest round.
 *
 * This global variable is accessed without any synchronization. Its value
 * being slightly inaccurate doesn't harm the fairness properties of the
 * scheduling and load balancing algorithms.
 *
 * There can be moderate bouncing on this word so give it its own cache line.
 */
static struct {
    volatile unsigned long value __aligned(CPU_L1_SIZE);
} thread_ts_highest_round_struct;

#define thread_ts_highest_round (thread_ts_highest_round_struct.value)

/*
 * List of threads pending for destruction by the reaper.
 */
static struct mutex thread_reap_lock;
static struct condition thread_reap_condition;
static struct list thread_reap_list;

struct thread_reap_waiter {
    struct list node;
    struct thread *thread;
};

static void __init
thread_runq_init_rt(struct thread_runq *runq)
{
    struct thread_rt_runq *rt_runq;
    size_t i;

    rt_runq = &runq->rt_runq;
    rt_runq->bitmap = 0;

    for (i = 0; i < ARRAY_SIZE(rt_runq->threads); i++)
        list_init(&rt_runq->threads[i]);
}

static void __init
thread_ts_group_init(struct thread_ts_group *group)
{
    list_init(&group->threads);
    group->weight = 0;
    group->work = 0;
}

static void __init
thread_ts_runq_init(struct thread_ts_runq *ts_runq)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(ts_runq->group_array); i++)
        thread_ts_group_init(&ts_runq->group_array[i]);

    list_init(&ts_runq->groups);
    list_init(&ts_runq->threads);
    ts_runq->nr_threads = 0;
    ts_runq->weight = 0;
    ts_runq->work = 0;
}

static void __init
thread_runq_init_ts(struct thread_runq *runq)
{
    runq->ts_weight = 0;
    runq->ts_runq_active = &runq->ts_runqs[0];
    runq->ts_runq_expired = &runq->ts_runqs[1];
    thread_ts_runq_init(runq->ts_runq_active);
    thread_ts_runq_init(runq->ts_runq_expired);
}

static void __init
thread_runq_init(struct thread_runq *runq, struct thread *booter)
{
    spinlock_init(&runq->lock);
    runq->current = booter;
    runq->nr_threads = 0;
    thread_runq_init_rt(runq);
    thread_runq_init_ts(runq);
    runq->balancer = NULL;
    runq->idler = NULL;
    runq->idle_balance_ticks = (unsigned int)-1;
}

static inline int
thread_runq_id(struct thread_runq *runq)
{
    return runq - thread_runqs;
}

static inline struct thread_runq *
thread_runq_local(void)
{
    assert(!thread_preempt_enabled() || thread_pinned());
    return &thread_runqs[cpu_id()];
}

static inline void
thread_set_flag(struct thread *thread, unsigned long flag)
{
    atomic_or(&thread->flags, flag);
}

static inline void
thread_clear_flag(struct thread *thread, unsigned long flag)
{
    atomic_and(&thread->flags, ~flag);
}

static inline int
thread_test_flag(struct thread *thread, unsigned long flag)
{
    barrier();
    return ((thread->flags & flag) != 0);
}

static void
thread_runq_add(struct thread_runq *runq, struct thread *thread)
{
    assert(!cpu_intr_enabled());
    spinlock_assert_locked(&runq->lock);

    thread_sched_ops[thread->sched_class].add(runq, thread);

    if (runq->nr_threads == 0)
        bitmap_set_atomic(thread_active_runqs, thread_runq_id(runq));

    runq->nr_threads++;

    if (thread->sched_class < runq->current->sched_class)
        thread_set_flag(runq->current, THREAD_RESCHEDULE);

    thread->runq = runq;
}

static void
thread_runq_remove(struct thread_runq *runq, struct thread *thread)
{
    assert(!cpu_intr_enabled());
    spinlock_assert_locked(&runq->lock);

    runq->nr_threads--;

    if (runq->nr_threads == 0)
        bitmap_clear_atomic(thread_active_runqs, thread_runq_id(runq));

    thread_sched_ops[thread->sched_class].remove(runq, thread);
}

static void
thread_runq_put_prev(struct thread_runq *runq, struct thread *thread)
{
    assert(!cpu_intr_enabled());
    spinlock_assert_locked(&runq->lock);

    thread_sched_ops[thread->sched_class].put_prev(runq, thread);
}

static struct thread *
thread_runq_get_next(struct thread_runq *runq)
{
    struct thread *thread;
    unsigned int i;

    assert(!cpu_intr_enabled());
    spinlock_assert_locked(&runq->lock);

    for (i = 0; i < ARRAY_SIZE(thread_sched_ops); i++) {
        thread = thread_sched_ops[i].get_next(runq);

        if (thread != NULL) {
            runq->current = thread;
            return thread;
        }
    }

    /* The idle class should never be empty */
    panic("thread: unable to find next thread");
}

static void
thread_runq_wakeup(struct thread_runq *runq, struct thread *thread)
{
    assert(!cpu_intr_enabled());
    spinlock_assert_locked(&runq->lock);
    assert(thread->state == THREAD_RUNNING);

    thread_runq_add(runq, thread);

    if ((runq != thread_runq_local())
        && thread_test_flag(runq->current, THREAD_RESCHEDULE)) {
        /*
         * Make the new flags globally visible before sending the
         * rescheduling request. This barrier pairs with the one implied
         * by the rescheduling IPI.
         */
        mb_store();

        tcb_send_reschedule(thread_runq_id(runq));
    }
}

static void
thread_runq_wakeup_balancer(struct thread_runq *runq)
{
    if (runq->balancer->state == THREAD_RUNNING)
        return;

    runq->balancer->state = THREAD_RUNNING;
    thread_runq_wakeup(runq, runq->balancer);
}

static struct thread_runq *
thread_runq_schedule(struct thread_runq *runq, struct thread *prev)
{
    struct thread *next;

    assert(prev->preempt == 2);
    assert(!cpu_intr_enabled());
    spinlock_assert_locked(&runq->lock);

    thread_clear_flag(prev, THREAD_RESCHEDULE);
    thread_runq_put_prev(runq, prev);

    if (prev->state != THREAD_RUNNING) {
        thread_runq_remove(runq, prev);

        if ((runq->nr_threads == 0) && (prev != runq->balancer))
            thread_runq_wakeup_balancer(runq);
    }

    next = thread_runq_get_next(runq);
    assert((next != runq->idler) || (runq->nr_threads == 0));

    if (prev != next) {
        if ((prev->task != next->task) && (next->task != kernel_task))
            pmap_load(next->task->map->pmap);

        /*
         * That's where the true context switch occurs. The next thread must
         * unlock the run queue and reenable preemption.
         */
        tcb_switch(&prev->tcb, &next->tcb);

        /*
         * When dispatched again, the thread might have been moved to another
         * processor.
         */
        runq = thread_runq_local();
    }

    return runq;
}

static void
thread_runq_double_lock(struct thread_runq *a, struct thread_runq *b)
{
    assert(!cpu_intr_enabled());
    assert(!thread_preempt_enabled());
    assert(a != b);

    if (a < b) {
        spinlock_lock(&a->lock);
        spinlock_lock(&b->lock);
    } else {
        spinlock_lock(&b->lock);
        spinlock_lock(&a->lock);
    }
}

static void
thread_sched_rt_init_thread(struct thread *thread, unsigned short priority)
{
    assert(priority <= THREAD_SCHED_RT_PRIO_MAX);
    thread->rt_ctx.priority = priority;
    thread->rt_ctx.time_slice = THREAD_DEFAULT_RR_TIME_SLICE;
}

static struct thread_runq *
thread_sched_rt_select_runq(void)
{
    struct thread_runq *runq;

    runq = thread_runq_local();
    spinlock_lock(&runq->lock);
    return runq;
}

static void
thread_sched_rt_add(struct thread_runq *runq, struct thread *thread)
{
    struct thread_rt_runq *rt_runq;
    struct list *threads;

    rt_runq = &runq->rt_runq;
    threads = &rt_runq->threads[thread->rt_ctx.priority];
    list_insert_tail(threads, &thread->rt_ctx.node);

    if (list_singular(threads))
        rt_runq->bitmap |= (1U << thread->rt_ctx.priority);

    if ((thread->sched_class == runq->current->sched_class)
        && (thread->rt_ctx.priority > runq->current->rt_ctx.priority))
        thread_set_flag(runq->current, THREAD_RESCHEDULE);
}

static void
thread_sched_rt_remove(struct thread_runq *runq, struct thread *thread)
{
    struct thread_rt_runq *rt_runq;
    struct list *threads;

    rt_runq = &runq->rt_runq;
    threads = &rt_runq->threads[thread->rt_ctx.priority];
    list_remove(&thread->rt_ctx.node);

    if (list_empty(threads))
        rt_runq->bitmap &= ~(1U << thread->rt_ctx.priority);
}

static void
thread_sched_rt_put_prev(struct thread_runq *runq, struct thread *thread)
{
    thread_sched_rt_add(runq, thread);
}

static struct thread *
thread_sched_rt_get_next(struct thread_runq *runq)
{
    struct thread_rt_runq *rt_runq;
    struct thread *thread;
    struct list *threads;
    unsigned int priority;

    rt_runq = &runq->rt_runq;

    if (rt_runq->bitmap == 0)
        return NULL;

    priority = THREAD_SCHED_RT_PRIO_MAX - __builtin_clz(rt_runq->bitmap);
    threads = &rt_runq->threads[priority];
    assert(!list_empty(threads));
    thread = list_first_entry(threads, struct thread, rt_ctx.node);
    thread_sched_rt_remove(runq, thread);
    return thread;
}

static void
thread_sched_rt_tick(struct thread_runq *runq, struct thread *thread)
{
    (void)runq;

    if (thread->sched_policy != THREAD_SCHED_POLICY_RR)
        return;

    thread->rt_ctx.time_slice--;

    if (thread->rt_ctx.time_slice > 0)
        return;

    thread->rt_ctx.time_slice = THREAD_DEFAULT_RR_TIME_SLICE;
    thread_set_flag(thread, THREAD_RESCHEDULE);
}

static inline unsigned short
thread_sched_ts_prio2weight(unsigned short priority)
{
    return ((priority + 1) * THREAD_TS_ROUND_SLICE_BASE);
}

static void
thread_sched_ts_init_thread(struct thread *thread, unsigned short priority)
{
    assert(priority <= THREAD_SCHED_TS_PRIO_MAX);
    thread->ts_ctx.ts_runq = NULL;
    thread->ts_ctx.round = 0;
    thread->ts_ctx.priority = priority;
    thread->ts_ctx.weight = thread_sched_ts_prio2weight(priority);
    thread->ts_ctx.work = 0;
}

static struct thread_runq *
thread_sched_ts_select_runq(void)
{
    struct thread_runq *runq, *tmp;
    int i, nr_runqs;
    long delta;

    nr_runqs = cpu_count();

    bitmap_for_each_zero(thread_active_runqs, nr_runqs, i) {
        runq = &thread_runqs[i];

        spinlock_lock(&runq->lock);

        /* The run queue really is idle, return it */
        if (runq->current == runq->idler)
            goto out;

        spinlock_unlock(&runq->lock);
    }

    runq = &thread_runqs[0];

    spinlock_lock(&runq->lock);

    for (i = 1; i < nr_runqs; i++) {
        tmp = &thread_runqs[i];

        spinlock_lock(&tmp->lock);

        /* A run queue may have become idle */
        if (tmp->current == tmp->idler) {
            spinlock_unlock(&runq->lock);
            runq = tmp;
            goto out;
        }

        /*
         * The run queue isn't idle, but there are no time-sharing thread,
         * which means there are real-time threads.
         */
        if (tmp->ts_weight == 0) {
            spinlock_unlock(&tmp->lock);
            continue;
        }

        delta = (long)(tmp->ts_round - runq->ts_round);

        /* Look for the least loaded of the run queues in the highest round */
        if ((delta > 0)
            || ((delta == 0) && (tmp->ts_weight < runq->ts_weight))) {
            spinlock_unlock(&runq->lock);
            runq = tmp;
            continue;
        }

        spinlock_unlock(&tmp->lock);
    }

out:
    return runq;
}

static unsigned int
thread_sched_ts_enqueue_scale(unsigned int work, unsigned int old_weight,
                              unsigned int new_weight)
{
    assert(old_weight != 0);

#ifndef __LP64__
    if (likely((work < 0x10000) && (new_weight < 0x10000)))
        return (work * new_weight) / old_weight;
#endif /* __LP64__ */

    return (unsigned int)(((unsigned long long)work * new_weight) / old_weight);
}

static void
thread_sched_ts_enqueue(struct thread_ts_runq *ts_runq, unsigned long round,
                        struct thread *thread)
{
    struct thread_ts_group *group, *tmp;
    struct list *node, *init_node;
    unsigned int group_weight, total_weight;

    assert(thread->ts_ctx.ts_runq == NULL);

    group = &ts_runq->group_array[thread->ts_ctx.priority];
    group_weight = group->weight + thread->ts_ctx.weight;
    total_weight = ts_runq->weight + thread->ts_ctx.weight;
    node = (group->weight == 0)
           ? list_last(&ts_runq->groups)
           : list_prev(&group->node);
    init_node = node;

    while (!list_end(&ts_runq->groups, node)) {
        tmp = list_entry(node, struct thread_ts_group, node);

        if (tmp->weight >= group_weight)
            break;

        node = list_prev(node);
    }

    if (group->weight == 0)
        list_insert_after(node, &group->node);
    else if (node != init_node) {
        list_remove(&group->node);
        list_insert_after(node, &group->node);
    }

    /*
     * XXX Unfairness can occur if the run queue round wraps around and the
     * thread is "lucky" enough to have the same round value. This should be
     * rare and harmless otherwise.
     */
    if (thread->ts_ctx.round == round) {
        ts_runq->work += thread->ts_ctx.work;
        group->work += thread->ts_ctx.work;
    } else {
        unsigned int group_work, thread_work;

        if (ts_runq->weight == 0)
            thread_work = 0;
        else {
            group_work = (group->weight == 0)
                         ? thread_sched_ts_enqueue_scale(ts_runq->work,
                                                         ts_runq->weight,
                                                         thread->ts_ctx.weight)
                         : thread_sched_ts_enqueue_scale(group->work,
                                                         group->weight,
                                                         group_weight);
            thread_work = group_work - group->work;
            ts_runq->work += thread_work;
            group->work = group_work;
        }

        thread->ts_ctx.round = round;
        thread->ts_ctx.work = thread_work;
    }

    ts_runq->nr_threads++;
    ts_runq->weight = total_weight;
    group->weight = group_weight;

    /* Insert at the front of the group to improve interactivity */
    list_insert(&group->threads, &thread->ts_ctx.group_node);
    list_insert_tail(&ts_runq->threads, &thread->ts_ctx.runq_node);
    thread->ts_ctx.ts_runq = ts_runq;
}

static void
thread_sched_ts_restart(struct thread_runq *runq)
{
    struct thread_ts_runq *ts_runq;
    struct list *node;

    ts_runq = runq->ts_runq_active;
    node = list_first(&ts_runq->groups);
    assert(node != NULL);
    ts_runq->current = list_entry(node, struct thread_ts_group, node);

    if (runq->current->sched_class == THREAD_SCHED_CLASS_TS)
        thread_set_flag(runq->current, THREAD_RESCHEDULE);
}

static void
thread_sched_ts_add(struct thread_runq *runq, struct thread *thread)
{
    unsigned int total_weight;

    if (runq->ts_weight == 0)
        runq->ts_round = thread_ts_highest_round;

    total_weight = runq->ts_weight + thread->ts_ctx.weight;

    /* TODO Limit the maximum number of threads to prevent this situation */
    if (total_weight < runq->ts_weight)
        panic("thread: weight overflow");

    runq->ts_weight = total_weight;
    thread_sched_ts_enqueue(runq->ts_runq_active, runq->ts_round, thread);
    thread_sched_ts_restart(runq);
}

static void
thread_sched_ts_dequeue(struct thread *thread)
{
    struct thread_ts_runq *ts_runq;
    struct thread_ts_group *group, *tmp;
    struct list *node, *init_node;

    assert(thread->ts_ctx.ts_runq != NULL);

    ts_runq = thread->ts_ctx.ts_runq;
    group = &ts_runq->group_array[thread->ts_ctx.priority];

    thread->ts_ctx.ts_runq = NULL;
    list_remove(&thread->ts_ctx.runq_node);
    list_remove(&thread->ts_ctx.group_node);
    ts_runq->work -= thread->ts_ctx.work;
    group->work -= thread->ts_ctx.work;
    ts_runq->weight -= thread->ts_ctx.weight;
    group->weight -= thread->ts_ctx.weight;
    ts_runq->nr_threads--;

    if (group->weight == 0)
        list_remove(&group->node);
    else {
        node = list_next(&group->node);
        init_node = node;

        while (!list_end(&ts_runq->groups, node)) {
            tmp = list_entry(node, struct thread_ts_group, node);

            if (tmp->weight <= group->weight)
                break;

            node = list_next(node);
        }

        if (node != init_node) {
            list_remove(&group->node);
            list_insert_before(node, &group->node);
        }
    }
}

static void
thread_sched_ts_remove(struct thread_runq *runq, struct thread *thread)
{
    struct thread_ts_runq *ts_runq;

    runq->ts_weight -= thread->ts_ctx.weight;
    ts_runq = thread->ts_ctx.ts_runq;
    thread_sched_ts_dequeue(thread);

    if (ts_runq == runq->ts_runq_active) {
        if (ts_runq->nr_threads == 0)
            thread_runq_wakeup_balancer(runq);
        else
            thread_sched_ts_restart(runq);
    }
}

static void
thread_sched_ts_deactivate(struct thread_runq *runq, struct thread *thread)
{
    assert(thread->ts_ctx.ts_runq == runq->ts_runq_active);
    assert(thread->ts_ctx.round == runq->ts_round);

    thread_sched_ts_dequeue(thread);
    thread->ts_ctx.round++;
    thread->ts_ctx.work -= thread->ts_ctx.weight;
    thread_sched_ts_enqueue(runq->ts_runq_expired, runq->ts_round + 1, thread);

    if (runq->ts_runq_active->nr_threads == 0)
        thread_runq_wakeup_balancer(runq);
}

static void
thread_sched_ts_put_prev(struct thread_runq *runq, struct thread *thread)
{
    struct thread_ts_runq *ts_runq;
    struct thread_ts_group *group;

    ts_runq = runq->ts_runq_active;
    group = &ts_runq->group_array[thread->ts_ctx.priority];
    list_insert_tail(&group->threads, &thread->ts_ctx.group_node);

    if (thread->ts_ctx.work >= thread->ts_ctx.weight)
        thread_sched_ts_deactivate(runq, thread);
}

static int
thread_sched_ts_ratio_exceeded(struct thread_ts_group *current,
                               struct thread_ts_group *next)
{
    unsigned long long a, b;

#ifndef __LP64__
    unsigned int ia, ib;

    if (likely((current->weight < 0x10000) && (next->weight < 0x10000))) {
        ia = (current->work + 1) * next->weight;
        ib = (next->work + 1) * current->weight;
        return ia > ib;
    }
#endif /* __LP64__ */

    a = ((unsigned long long)current->work + 1) * next->weight;
    b = ((unsigned long long)next->work + 1) * current->weight;
    return a > b;
}

static struct thread *
thread_sched_ts_get_next(struct thread_runq *runq)
{
    struct thread_ts_runq *ts_runq;
    struct thread_ts_group *group, *next;
    struct thread *thread;
    struct list *node;

    ts_runq = runq->ts_runq_active;

    if (ts_runq->nr_threads == 0)
        return NULL;

    group = ts_runq->current;
    node = list_next(&group->node);

    if (list_end(&ts_runq->groups, node)) {
        node = list_first(&ts_runq->groups);
        group = list_entry(node, struct thread_ts_group, node);
    } else {
        next = list_entry(node, struct thread_ts_group, node);

        if (thread_sched_ts_ratio_exceeded(group, next))
            group = next;
        else {
            node = list_first(&ts_runq->groups);
            group = list_entry(node, struct thread_ts_group, node);
        }
    }

    ts_runq->current = group;
    node = list_first(&group->threads);
    thread = list_entry(node, struct thread, ts_ctx.group_node);
    list_remove(node);
    return thread;
}

static void
thread_sched_ts_tick(struct thread_runq *runq, struct thread *thread)
{
    struct thread_ts_runq *ts_runq;
    struct thread_ts_group *group;

    ts_runq = runq->ts_runq_active;
    ts_runq->work++;
    group = &ts_runq->group_array[thread->ts_ctx.priority];
    group->work++;
    thread_set_flag(thread, THREAD_RESCHEDULE);
    thread->ts_ctx.work++;
}

static void
thread_sched_ts_start_next_round(struct thread_runq *runq)
{
    struct thread_ts_runq *tmp;
    long delta;

    tmp = runq->ts_runq_expired;
    runq->ts_runq_expired = runq->ts_runq_active;
    runq->ts_runq_active = tmp;

    if (runq->ts_runq_active->nr_threads != 0) {
        runq->ts_round++;
        delta = (long)(runq->ts_round - thread_ts_highest_round);

        if (delta > 0)
            thread_ts_highest_round = runq->ts_round;

        thread_sched_ts_restart(runq);
    }
}

/*
 * Check that a remote run queue satisfies the minimum migration requirements.
 */
static int
thread_sched_ts_balance_eligible(struct thread_runq *runq,
                                 unsigned long highest_round)
{
    unsigned int nr_threads;

    if (runq->ts_weight == 0)
        return 0;

    if ((runq->ts_round != highest_round)
        && (runq->ts_round != (highest_round - 1)))
        return 0;

    nr_threads = runq->ts_runq_active->nr_threads
                 + runq->ts_runq_expired->nr_threads;

    if ((nr_threads == 0)
        || ((nr_threads == 1)
            && (runq->current->sched_class == THREAD_SCHED_CLASS_TS)))
        return 0;

    return 1;
}

/*
 * Try to find the most suitable run queue from which to pull threads.
 */
static struct thread_runq *
thread_sched_ts_balance_scan(struct thread_runq *runq,
                             unsigned long highest_round)
{
    struct thread_runq *remote_runq, *tmp;
    unsigned long flags;
    int i, nr_runqs;

    nr_runqs = cpu_count();
    remote_runq = NULL;

    thread_preempt_disable();
    flags = cpu_intr_save();

    bitmap_for_each(thread_active_runqs, nr_runqs, i) {
        tmp = &thread_runqs[i];

        if (tmp == runq)
            continue;

        spinlock_lock(&tmp->lock);

        if (!thread_sched_ts_balance_eligible(tmp, highest_round)) {
            spinlock_unlock(&tmp->lock);
            continue;
        }

        if (remote_runq == NULL) {
            remote_runq = tmp;
            continue;
        }

        if (tmp->ts_weight > remote_runq->ts_weight) {
            spinlock_unlock(&remote_runq->lock);
            remote_runq = tmp;
            continue;
        }

        spinlock_unlock(&tmp->lock);
    }

    if (remote_runq != NULL)
        spinlock_unlock(&remote_runq->lock);

    cpu_intr_restore(flags);
    thread_preempt_enable();

    return remote_runq;
}

static unsigned int
thread_sched_ts_balance_pull(struct thread_runq *runq,
                             struct thread_runq *remote_runq,
                             struct thread_ts_runq *ts_runq,
                             unsigned int nr_pulls)
{
    struct thread *thread, *tmp;

    list_for_each_entry_safe(&ts_runq->threads, thread, tmp, ts_ctx.runq_node) {
        if (thread == remote_runq->current)
            continue;

        /*
         * The pinned counter is changed without explicit synchronization.
         * However, it can only be changed by its owning thread. As threads
         * currently running aren't considered for migration, the thread had
         * to be preempted, and invoke the scheduler, which globally acts
         * as a memory barrier. As a result, there is strong ordering between
         * changing the pinned counter and setting the current thread of a
         * run queue. Enforce the same ordering on the pulling processor.
         */
        mb_load();

        if (thread->pinned)
            continue;

        /*
         * Make sure at least one thread is pulled if possible. If one or more
         * thread has already been pulled, take weights into account.
         */
        if ((nr_pulls != 0)
            && ((runq->ts_weight + thread->ts_ctx.weight)
                > (remote_runq->ts_weight - thread->ts_ctx.weight)))
            break;

        thread_runq_remove(remote_runq, thread);

        /* Don't discard the work already accounted for */
        thread->ts_ctx.round = runq->ts_round;

        thread_runq_add(runq, thread);
        nr_pulls++;

        if (nr_pulls == THREAD_MAX_MIGRATIONS)
            break;
    }

    return nr_pulls;
}

static unsigned int
thread_sched_ts_balance_migrate(struct thread_runq *runq,
                                struct thread_runq *remote_runq,
                                unsigned long highest_round)
{
    unsigned int nr_pulls;

    nr_pulls = 0;

    if (!thread_sched_ts_balance_eligible(remote_runq, highest_round))
        goto out;

    nr_pulls = thread_sched_ts_balance_pull(runq, remote_runq,
                                            remote_runq->ts_runq_active, 0);

    if (nr_pulls == THREAD_MAX_MIGRATIONS)
        goto out;

    /*
     * Threads in the expired queue of a processor in round highest are
     * actually in round highest + 1.
     */
    if (remote_runq->ts_round != highest_round)
        nr_pulls = thread_sched_ts_balance_pull(runq, remote_runq,
                                                remote_runq->ts_runq_expired,
                                                nr_pulls);

out:
    return nr_pulls;
}

/*
 * Inter-processor load balancing for time-sharing threads.
 *
 * Preemption must be disabled, and the local run queue must be locked when
 * calling this function. If balancing actually occurs, the lock will be
 * released and preemption enabled when needed.
 */
static void
thread_sched_ts_balance(struct thread_runq *runq, unsigned long *flags)
{
    struct thread_runq *remote_runq;
    unsigned long highest_round;
    unsigned int nr_migrations;
    int i, nr_runqs;

    /*
     * Grab the highest round now and only use the copy so the value is stable
     * during the balancing operation.
     */
    highest_round = thread_ts_highest_round;

    if ((runq->ts_round != highest_round)
        && (runq->ts_runq_expired->nr_threads != 0))
        goto no_migration;

    spinlock_unlock_intr_restore(&runq->lock, *flags);
    thread_preempt_enable();

    remote_runq = thread_sched_ts_balance_scan(runq, highest_round);

    if (remote_runq != NULL) {
        thread_preempt_disable();
        *flags = cpu_intr_save();
        thread_runq_double_lock(runq, remote_runq);
        nr_migrations = thread_sched_ts_balance_migrate(runq, remote_runq,
                                                        highest_round);
        spinlock_unlock(&remote_runq->lock);

        if (nr_migrations != 0)
            return;

        spinlock_unlock_intr_restore(&runq->lock, *flags);
        thread_preempt_enable();
    }

    /*
     * The scan or the migration failed. As a fallback, make another, simpler
     * pass on every run queue, and stop as soon as at least one thread could
     * be successfully pulled.
     */

    for (i = 0, nr_runqs = cpu_count(); i < nr_runqs; i++) {
        remote_runq = &thread_runqs[i];

        if (remote_runq == runq)
            continue;

        thread_preempt_disable();
        *flags = cpu_intr_save();
        thread_runq_double_lock(runq, remote_runq);
        nr_migrations = thread_sched_ts_balance_migrate(runq, remote_runq,
                                                        highest_round);
        spinlock_unlock(&remote_runq->lock);

        if (nr_migrations != 0)
            return;

        spinlock_unlock_intr_restore(&runq->lock, *flags);
        thread_preempt_enable();
    }

    thread_preempt_disable();
    spinlock_lock_intr_save(&runq->lock, flags);

no_migration:

    /*
     * No thread could be migrated. Check the active run queue, as another
     * processor might have added threads while the balancer was running.
     * If the run queue is still empty, switch to the next round. The run
     * queue lock must remain held until the next scheduling decision to
     * prevent a remote balancer thread from stealing active threads.
     */
    if (runq->ts_runq_active->nr_threads == 0)
        thread_sched_ts_start_next_round(runq);
}

static void
thread_sched_idle_init_thread(struct thread *thread, unsigned short priority)
{
    (void)thread;
    (void)priority;
}

static struct thread_runq *
thread_sched_idle_select_runq(void)
{
    panic("thread: idler threads cannot be awaken");
}

static void __noreturn
thread_sched_idle_panic(void)
{
    panic("thread: only idle threads are allowed in the idle class");
}

static void
thread_sched_idle_add(struct thread_runq *runq, struct thread *thread)
{
    (void)runq;
    (void)thread;

    thread_sched_idle_panic();
}

static void
thread_sched_idle_remove(struct thread_runq *runq, struct thread *thread)
{
    (void)runq;
    (void)thread;

    thread_sched_idle_panic();
}

static void
thread_sched_idle_put_prev(struct thread_runq *runq, struct thread *thread)
{
    (void)runq;
    (void)thread;
}

static struct thread *
thread_sched_idle_get_next(struct thread_runq *runq)
{
    return runq->idler;
}

static void
thread_sched_idle_tick(struct thread_runq *runq, struct thread *thread)
{
    (void)runq;
    (void)thread;
}

static void __init
thread_bootstrap_common(unsigned int cpu)
{
    struct thread *booter;

    booter = &thread_booters[cpu];

    /* Initialize only what's needed during bootstrap */
    booter->flags = 0;
    booter->preempt = 1;
    booter->sched_class = THREAD_SCHED_CLASS_IDLE;

    thread_runq_init(&thread_runqs[cpu], booter);
    tcb_set_current(&booter->tcb);
}

void __init
thread_bootstrap(void)
{
    struct thread_sched_ops *ops;

    thread_policy_table[THREAD_SCHED_POLICY_FIFO] = THREAD_SCHED_CLASS_RT;
    thread_policy_table[THREAD_SCHED_POLICY_RR] = THREAD_SCHED_CLASS_RT;
    thread_policy_table[THREAD_SCHED_POLICY_TS] = THREAD_SCHED_CLASS_TS;
    thread_policy_table[THREAD_SCHED_POLICY_IDLE] = THREAD_SCHED_CLASS_IDLE;

    ops = &thread_sched_ops[THREAD_SCHED_CLASS_RT];
    ops->init_thread = thread_sched_rt_init_thread;
    ops->select_runq = thread_sched_rt_select_runq;
    ops->add = thread_sched_rt_add;
    ops->remove = thread_sched_rt_remove;
    ops->put_prev = thread_sched_rt_put_prev;
    ops->get_next = thread_sched_rt_get_next;
    ops->tick = thread_sched_rt_tick;

    ops = &thread_sched_ops[THREAD_SCHED_CLASS_TS];
    ops->init_thread = thread_sched_ts_init_thread;
    ops->select_runq = thread_sched_ts_select_runq;
    ops->add = thread_sched_ts_add;
    ops->remove = thread_sched_ts_remove;
    ops->put_prev = thread_sched_ts_put_prev;
    ops->get_next = thread_sched_ts_get_next;
    ops->tick = thread_sched_ts_tick;

    ops = &thread_sched_ops[THREAD_SCHED_CLASS_IDLE];
    ops->init_thread = thread_sched_idle_init_thread;
    ops->select_runq = thread_sched_idle_select_runq;
    ops->add = thread_sched_idle_add;
    ops->remove = thread_sched_idle_remove;
    ops->put_prev = thread_sched_idle_put_prev;
    ops->get_next = thread_sched_idle_get_next;
    ops->tick = thread_sched_idle_tick;

    bitmap_zero(thread_active_runqs, MAX_CPUS);

    thread_ts_highest_round = THREAD_TS_INITIAL_ROUND;

    thread_bootstrap_common(0);
}

void __init
thread_ap_bootstrap(void)
{
    thread_bootstrap_common(cpu_id());
}

static void
thread_main(void)
{
    struct thread *thread;

    assert(!cpu_intr_enabled());
    assert(!thread_preempt_enabled());

    spinlock_unlock(&thread_runq_local()->lock);
    cpu_intr_enable();
    thread_preempt_enable();

    thread = thread_self();
    thread->fn(thread->arg);
    thread_exit();
}

static void
thread_init_sched(struct thread *thread, unsigned short priority)
{
    thread_sched_ops[thread->sched_class].init_thread(thread, priority);
}

static void
thread_init(struct thread *thread, void *stack, const struct thread_attr *attr,
            void (*fn)(void *), void *arg)
{
    const char *name;
    struct task *task;

    if (attr == NULL)
        attr = &thread_default_attr;

    task = (attr->task == NULL) ? thread_self()->task : attr->task;
    assert(task != NULL);
    name = (attr->name == NULL) ? task->name : attr->name;
    assert(name != NULL);
    assert(attr->sched_policy < THREAD_NR_SCHED_POLICIES);

    /*
     * The expected interrupt, preemption and run queue lock state when
     * dispatching a thread is :
     *  - interrupts disabled
     *  - preemption disabled
     *  - run queue locked
     *
     * Locking the run queue increases the preemption counter once more,
     * making its value 2.
     */
    tcb_init(&thread->tcb, stack, thread_main);
    thread->flags = 0;
    thread->runq = NULL;
    thread->state = THREAD_SLEEPING;
    thread->preempt = 2;
    thread->pinned = 0;
    thread->sched_policy = attr->sched_policy;
    thread->sched_class = thread_policy_table[attr->sched_policy];
    thread_init_sched(thread, attr->priority);
    thread->task = task;
    thread->stack = stack;
    strlcpy(thread->name, name, sizeof(thread->name));
    thread->fn = fn;
    thread->arg = arg;

    task_add_thread(task, thread);
}

static void
thread_lock_runq(struct thread *thread, unsigned long *flags)
{
    struct thread_runq *runq;

    assert(thread != thread_self());

    for (;;) {
        runq = thread->runq;

        spinlock_lock_intr_save(&runq->lock, flags);

        if (runq == thread->runq)
            return;

        spinlock_unlock_intr_restore(&runq->lock, *flags);
    }
}

static void
thread_unlock_runq(struct thread *thread, unsigned long flags)
{
    spinlock_unlock_intr_restore(&thread->runq->lock, flags);
}

static void
thread_destroy(struct thread *thread)
{
    unsigned long flags, state;

    do {
        thread_lock_runq(thread, &flags);
        state = thread->state;
        thread_unlock_runq(thread, flags);
    } while (state != THREAD_DEAD);

    task_remove_thread(thread->task, thread);
    kmem_cache_free(&thread_stack_cache, thread->stack);
    kmem_cache_free(&thread_cache, thread);
}

static void
thread_reaper(void *arg)
{
    struct thread_reap_waiter *tmp;
    struct list waiters;

    (void)arg;

    for (;;) {
        mutex_lock(&thread_reap_lock);

        while (list_empty(&thread_reap_list))
            condition_wait(&thread_reap_condition, &thread_reap_lock);

        list_set_head(&waiters, &thread_reap_list);
        list_init(&thread_reap_list);

        mutex_unlock(&thread_reap_lock);

        while (!list_empty(&waiters)) {
            tmp = list_first_entry(&waiters, struct thread_reap_waiter, node);
            list_remove(&tmp->node);
            thread_destroy(tmp->thread);
        }
    }

    /* Never reached */
}

static void __init
thread_setup_reaper(void)
{
    struct thread_attr attr;
    struct thread *thread;
    int error;

    mutex_init(&thread_reap_lock);
    condition_init(&thread_reap_condition);
    list_init(&thread_reap_list);

    attr.task = kernel_task;
    attr.name = "x15_reaper";
    attr.sched_policy = THREAD_SCHED_CLASS_TS;
    attr.priority = THREAD_SCHED_TS_PRIO_DEFAULT;
    error = thread_create(&thread, &attr, thread_reaper, NULL);

    if (error)
        panic("thread: unable to create reaper thread");
}

static void
thread_balancer_idle_tick(struct thread_runq *runq)
{
    assert(runq->idle_balance_ticks != 0);

    /*
     * Interrupts can occur early, at a time the balancer thread hasn't been
     * created yet.
     */
    if (runq->balancer == NULL)
        return;

    runq->idle_balance_ticks--;

    if (runq->idle_balance_ticks == 0)
        thread_runq_wakeup_balancer(runq);
}

static void
thread_balancer(void *arg)
{
    struct thread_runq *runq;
    struct thread *self;
    unsigned long flags;

    runq = arg;
    self = runq->balancer;
    assert(self == runq->balancer);

    thread_preempt_disable();
    spinlock_lock_intr_save(&runq->lock, &flags);

    for (;;) {
        runq->idle_balance_ticks = THREAD_IDLE_BALANCE_TICKS;
        self->state = THREAD_SLEEPING;
        runq = thread_runq_schedule(runq, self);
        assert(runq == arg);

        /*
         * This function may temporarily enable preemption and release the
         * run queue lock.
         */
        thread_sched_ts_balance(runq, &flags);
    }
}

static void __init
thread_setup_balancer(struct thread_runq *runq)
{
    char name[THREAD_NAME_SIZE];
    struct thread_runq *local_runq;
    struct thread_attr attr;
    struct thread *balancer;
    int error;

    snprintf(name, sizeof(name), "x15_balancer/%u", thread_runq_id(runq));
    attr.task = kernel_task;
    attr.name = name;
    attr.sched_policy = THREAD_SCHED_CLASS_RT;
    attr.priority = THREAD_SCHED_RT_PRIO_MIN;
    error = thread_create(&balancer, &attr, thread_balancer, runq);

    if (error)
        panic("thread: unable to create balancer thread");

    runq->balancer = balancer;

    /*
     * XXX Real-time threads are currently dispatched on the creator's run
     * queue.
     *
     * TODO Implement processor affinity and remove this kludge.
     */
    local_runq = thread_runq_local();

    if (runq != local_runq) {
        unsigned long flags;

        spinlock_lock_intr_save(&local_runq->lock, &flags);
        thread_runq_remove(local_runq, balancer);
        spinlock_unlock_intr_restore(&local_runq->lock, flags);

        spinlock_lock_intr_save(&runq->lock, &flags);
        thread_runq_add(runq, balancer);
        spinlock_unlock_intr_restore(&runq->lock, flags);
    }
}

static void
thread_idler(void *arg)
{
    (void)arg;

    for (;;)
        cpu_idle();
}

static void __init
thread_setup_idler(struct thread_runq *runq)
{
    char name[THREAD_NAME_SIZE];
    struct thread_attr attr;
    struct thread *idler;
    void *stack;

    idler = kmem_cache_alloc(&thread_cache);

    if (idler == NULL)
        panic("thread: unable to allocate idler thread");

    stack = kmem_cache_alloc(&thread_stack_cache);

    if (stack == NULL)
        panic("thread: unable to allocate idler thread stack");

    snprintf(name, sizeof(name), "x15_idler/%u", thread_runq_id(runq));
    attr.task = kernel_task;
    attr.name = name;
    attr.sched_policy = THREAD_SCHED_POLICY_IDLE;
    thread_init(idler, stack, &attr, thread_idler, NULL);

    /* An idler thread needs special tuning */
    idler->state = THREAD_RUNNING;
    runq->idler = idler;
}

static void __init
thread_setup_runq(struct thread_runq *runq)
{
    thread_setup_balancer(runq);
    thread_setup_idler(runq);
}

void __init
thread_setup(void)
{
    size_t i;

    kmem_cache_init(&thread_cache, "thread", sizeof(struct thread),
                    CPU_L1_SIZE, NULL, NULL, NULL, 0);
    kmem_cache_init(&thread_stack_cache, "thread_stack", STACK_SIZE,
                    DATA_ALIGN, NULL, NULL, NULL, 0);

    thread_setup_reaper();

    for (i = 0; i < cpu_count(); i++)
        thread_setup_runq(&thread_runqs[i]);
}

int
thread_create(struct thread **threadp, const struct thread_attr *attr,
              void (*fn)(void *), void *arg)

{
    struct thread *thread;
    void *stack;
    int error;

    thread = kmem_cache_alloc(&thread_cache);

    if (thread == NULL) {
        error = ERROR_NOMEM;
        goto error_thread;
    }

    stack = kmem_cache_alloc(&thread_stack_cache);

    if (stack == NULL) {
        error = ERROR_NOMEM;
        goto error_stack;
    }

    thread_init(thread, stack, attr, fn, arg);
    thread_wakeup(thread);

    *threadp = thread;
    return 0;

error_stack:
    kmem_cache_free(&thread_cache, thread);
error_thread:
    return error;
}

void
thread_exit(void)
{
    struct thread_reap_waiter waiter;
    struct thread_runq *runq;
    struct thread *thread;
    unsigned long flags;

    thread = thread_self();
    waiter.thread = thread;

    mutex_lock(&thread_reap_lock);

    list_insert_tail(&thread_reap_list, &waiter.node);
    condition_signal(&thread_reap_condition);

    /*
     * Disable preemption before releasing the mutex to make sure the current
     * thread becomes dead as soon as possible. This is important because the
     * reaper thread actively polls the thread state before destroying it.
     */
    thread_preempt_disable();

    mutex_unlock(&thread_reap_lock);

    runq = thread_runq_local();
    spinlock_lock_intr_save(&runq->lock, &flags);

    thread->state = THREAD_DEAD;

    runq = thread_runq_schedule(runq, thread);
    panic("thread: dead thread running");
}

void
thread_sleep(struct spinlock *interlock)
{
    struct thread_runq *runq;
    struct thread *thread;
    unsigned long flags;

    thread = thread_self();

    thread_preempt_disable();
    runq = thread_runq_local();
    spinlock_lock_intr_save(&runq->lock, &flags);
    spinlock_unlock(interlock);

    thread->state = THREAD_SLEEPING;

    runq = thread_runq_schedule(runq, thread);
    assert(thread->state == THREAD_RUNNING);

    spinlock_unlock_intr_restore(&runq->lock, flags);
    thread_preempt_enable();

    spinlock_lock(interlock);
}

void
thread_wakeup(struct thread *thread)
{
    struct thread_runq *runq;
    unsigned long flags;

    /*
     * There is at most one reference on threads that were never dispatched,
     * in which case there is no need to lock anything.
     */
    if (thread->runq == NULL) {
        assert(thread->state != THREAD_RUNNING);
        thread->state = THREAD_RUNNING;
    } else {
        thread_lock_runq(thread, &flags);

        if (thread->state == THREAD_RUNNING) {
            thread_unlock_runq(thread, flags);
            return;
        }

        thread->state = THREAD_RUNNING;
        thread_unlock_runq(thread, flags);
    }

    thread_preempt_disable();
    flags = cpu_intr_save();

    /* The returned run queue is locked */
    runq = thread_sched_ops[thread->sched_class].select_runq();
    thread_runq_wakeup(runq, thread);
    spinlock_unlock(&runq->lock);
    cpu_intr_restore(flags);
    thread_preempt_enable();
}

void __init
thread_run(void)
{
    struct thread_runq *runq;
    struct thread *thread;

    assert(cpu_intr_enabled());

    runq = thread_runq_local();
    thread = thread_self();
    assert(thread == runq->current);
    assert(thread->preempt == 1);

    cpu_intr_disable();
    spinlock_lock(&runq->lock);
    thread = thread_runq_get_next(thread_runq_local());

    if (thread->task != kernel_task)
        pmap_load(thread->task->map->pmap);

    tcb_load(&thread->tcb);
}

void
thread_reschedule(void)
{
    struct thread_runq *runq;
    struct thread *thread;
    unsigned long flags;

    thread = thread_self();

    if (!thread_test_flag(thread, THREAD_RESCHEDULE)
        || !thread_preempt_enabled())
        return;

    do {
        thread_preempt_disable();
        runq = thread_runq_local();
        spinlock_lock_intr_save(&runq->lock, &flags);
        runq = thread_runq_schedule(runq, thread);
        spinlock_unlock_intr_restore(&runq->lock, flags);
        thread_preempt_enable_no_resched();
    } while (thread_test_flag(thread, THREAD_RESCHEDULE));
}

void
thread_tick(void)
{
    struct thread_runq *runq;
    struct thread *thread;

    assert(!cpu_intr_enabled());
    assert(!thread_preempt_enabled());

    runq = thread_runq_local();
    thread = thread_self();

    spinlock_lock(&runq->lock);

    if (runq->nr_threads == 0)
        thread_balancer_idle_tick(runq);

    thread_sched_ops[thread->sched_class].tick(runq, thread);

    spinlock_unlock(&runq->lock);
}

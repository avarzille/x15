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
 * Thread control block.
 */

#ifndef _X86_TCB_H
#define _X86_TCB_H

#include <kern/assert.h>
#include <kern/macros.h>
#include <machine/cpu.h>
#include <machine/lapic.h>
#include <machine/trap.h>

/*
 * Architecture specific thread data.
 */
struct tcb {
    unsigned long bp;
    unsigned long sp;
    unsigned long ip;
};

/*
 * Initialize a TCB.
 *
 * Prepare the given stack for execution. The context is defined so that it
 * will call fn() with interrupts disabled when loaded.
 */
void tcb_init(struct tcb *tcb, void *stack, void (*fn)(void));

/*
 * Low level context switch function.
 */
void tcb_context_switch(struct tcb *prev, struct tcb *next);

static inline struct tcb *
tcb_current(void)
{
    return cpu_percpu_get_tcb();
}

static inline void
tcb_set_current(struct tcb *tcb)
{
    cpu_percpu_set_tcb(tcb);
}

/*
 * Load a TCB.
 *
 * Called with interrupts disabled. The caller context is lost.
 */
void __noreturn tcb_load(struct tcb *tcb);

/*
 * Context switch.
 *
 * Called with interrupts disabled.
 */
static inline void
tcb_switch(struct tcb *prev, struct tcb *next)
{
    assert(!cpu_intr_enabled());

    tcb_set_current(next);
    tcb_context_switch(prev, next);
}

/*
 * Dump the stack trace of a TCB.
 *
 * The thread associated to the TCB should not be running.
 */
void tcb_trace(const struct tcb *tcb);

/*
 * Send a rescheduling interrupt to a remote processor.
 */
static inline void
tcb_send_reschedule(unsigned int cpu)
{
    lapic_ipi_send(cpu, TRAP_RESCHEDULE);
}

/*
 * Interrupt handler for rescheduling requests.
 */
void tcb_reschedule_intr(struct trap_frame *frame);

#endif /* _X86_TCB_H */

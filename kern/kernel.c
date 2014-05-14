/*
 * Copyright (c) 2011-2014 Richard Braun.
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
 */

#include <kern/cpumap.h>
#include <kern/init.h>
#include <kern/kernel.h>
#include <kern/llsync.h>
#include <kern/panic.h>
#include <kern/rdxtree.h>
#include <kern/task.h>
#include <kern/thread.h>
#include <kern/work.h>
#include <machine/cpu.h>

void __init
kernel_main(void)
{
    assert(!cpu_intr_enabled());

    rdxtree_setup();
    cpumap_setup();
    task_setup();
    thread_setup();
    work_setup();
    llsync_setup();

    /*
     * Enabling application processors is done late in the boot process for
     * two reasons :
     *  - It's much simpler to bootstrap with interrupts disabled on all
     *    processors, enabling them only when necessary on the BSP.
     *  - Depending on the architecture, the pmap module could create per
     *    processor page tables. Once done, keeping the kernel page tables
     *    synchronized requires interrupts (and potentially scheduling)
     *    enabled on all processors.
     *
     * Anything done after this call and before running the scheduler must
     * not alter physical mappings.
     */
    cpu_mp_setup();

    thread_run_scheduler();

    /* Never reached */
}

void __init
kernel_ap_main(void)
{
    assert(!cpu_intr_enabled());

    thread_run_scheduler();

    /* Never reached */
}

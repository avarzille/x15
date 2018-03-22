/*
 * Copyright (c) 2012-2018 Richard Braun.
 * Copyright (c) 2018 Agustina Arzille.
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

#ifndef _KERN_STRACE_H
#define _KERN_STRACE_H

#include <stdint.h>
#include <machine/strace.h>

/*
 * Display a call trace.
 *
 * Attempt to resolve the given instruction pointer, then walk the calling
 * chain from the given frame pointer.
 */
void strace_show(uintptr_t ip, uintptr_t bp);

static __always_inline void
strace_dump(void)
{
    uintptr_t ip, bp;
    strace_get_frame_info(&ip, &bp);
    strace_show(ip, bp);
}

#endif /* _KERN_STRACE_H */

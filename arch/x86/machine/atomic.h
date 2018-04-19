/*
 * Copyright (c) 2012-2017 Richard Braun.
 * Copyright (c) 2017 Agustina Arzille.
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
 * Architecture-specific definitions for atomic operations.
 */

#ifndef X86_ATOMIC_H
#define X86_ATOMIC_H

#ifndef KERN_ATOMIC_H
#error "don't include <machine/atomic.h> directly, use <kern/atomic.h> instead"
#endif


#include <stdbool.h>
#include <stdint.h>

#include <kern/macros.h>

#ifndef __LP64__

/*
 * On i386, the compiler generates either an FP-stack read/write, or an SSE2
 * store/load to implement these 64-bit atomic operations. Since that's not
 * feasible in the kernel, fall back to cmpxchg8b. Note that, in this case,
 * loading becomes a potentially mutating operation, but it's not expected
 * to be a problem since atomic operations are normally not used on read-only
 * memory. Also note that this assumes the processor is at least an i586.
 */

/*
 * Temporarily discard qualifiers when loading 64-bits values with a
 * compare-and-swap operation.
 */
#define atomic_load_64(ptr, mo)                                             \
MACRO_BEGIN                                                                 \
    uint64_t ret_ = 0;                                                      \
                                                                            \
    __atomic_compare_exchange_n((uint64_t *)(ptr), &ret_, 0,                \
                                false, mo, __ATOMIC_RELAXED);               \
    ret_;                                                                   \
MACRO_END

#define atomic_load(ptr, mo)                                                \
    (typeof(*(ptr)))__builtin_choose_expr(sizeof(*(ptr)) == 8,              \
                                          atomic_load_64(ptr, mo),          \
                                          __atomic_load_n(ptr, mo))

#define atomic_store(ptr, val, mo)                                          \
MACRO_BEGIN                                                                 \
    if (sizeof(*(ptr)) != 8) {                                              \
        __atomic_store_n(ptr, val, mo);                                     \
    } else {                                                                \
        typeof(*(ptr)) oval_, nval_;                                        \
        bool done_;                                                         \
                                                                            \
        oval_ = *(ptr);                                                     \
        nval_ = (val);                                                      \
                                                                            \
        do {                                                                \
            done_ = __atomic_compare_exchange_n(ptr, &oval_, nval_,         \
                                                  false, mo,                \
                                                  __ATOMIC_RELAXED);        \
        } while (!done_);                                                   \
                                                                            \
    }                                                                       \
MACRO_END

#endif /* __LP64__ */

/*
 * XXX Clang seems to have trouble with 64-bits operations on 32-bits
 * processors.
 */
#if defined(__LP64__) || !defined(__clang__)

/*
 * Report that 64-bits operations are supported.
 */
#define ATOMIC_HAVE_64B_OPS

#endif /* __clang__ */

#endif /* X86_ATOMIC_H */

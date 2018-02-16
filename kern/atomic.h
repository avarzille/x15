/*
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
 * Type-generic memory-model aware atomic operations.
 *
 * TODO Replace mentions of "memory barriers" throughout the code with
 * C11 memory model terminology.
 */

#ifndef KERN_ATOMIC_H
#define KERN_ATOMIC_H

#include <stdbool.h>

#include <kern/macros.h>
#include <machine/atomic.h>

/*
 * Supported memory orders.
 */
#ifndef ATOMIC_RELAXED

#define ATOMIC_RELAXED   __ATOMIC_RELAXED
#define ATOMIC_CONSUME   __ATOMIC_CONSUME
#define ATOMIC_ACQUIRE   __ATOMIC_ACQUIRE
#define ATOMIC_RELEASE   __ATOMIC_RELEASE
#define ATOMIC_ACQ_REL   __ATOMIC_ACQ_REL
#define ATOMIC_SEQ_CST   __ATOMIC_SEQ_CST

#endif /* ATOMIC_RELAXED */

/*
 * Type-generic atomic operations.
 */

#ifndef atomic_fetch_add
#define atomic_fetch_add(ptr, val, mo)  __atomic_fetch_add(ptr, val, mo)
#endif

#ifndef atomic_fetch_sub
#define atomic_fetch_sub(ptr, val, mo)  __atomic_fetch_sub(ptr, val, mo)
#endif

#ifndef atomic_fetch_and
#define atomic_fetch_and(ptr, val, mo)  __atomic_fetch_and(ptr, val, mo)
#endif

#ifndef atomic_fetch_or
#define atomic_fetch_or(ptr, val, mo)   __atomic_fetch_or(ptr, val, mo)
#endif

#ifndef atomic_fetch_xor
#define atomic_fetch_xor(ptr, val, mo)  __atomic_fetch_xor(ptr, val, mo)
#endif

#ifndef atomic_add
#define atomic_add(ptr, val, mo)        (void)__atomic_add_fetch(ptr, val, mo)
#endif

#ifndef atomic_sub
#define atomic_sub(ptr, val, mo)        (void)__atomic_sub_fetch(ptr, val, mo)
#endif

#ifndef atomic_and
#define atomic_and(ptr, val, mo)        (void)__atomic_and_fetch(ptr, val, mo)
#endif

#ifndef atomic_or
#define atomic_or(ptr, val, mo)         (void)__atomic_or_fetch(ptr, val, mo)
#endif

#ifndef atomic_xor
#define atomic_xor(ptr, val, mo)        (void)__atomic_xor_fetch(ptr, val, mo)
#endif

#ifndef atomic_swap
#define atomic_swap(ptr, val, mo)       __atomic_exchange_n(ptr, val, mo)
#endif

/*
 * For compare-and-swap, deviate a little from the standard, and only
 * return the value before the comparison, leaving it up to the user to
 * determine whether the swap was actually performed or not.
 *
 * Also, note that the memory order in case of failure is relaxed. This is
 * because atomic CAS is typically used in a loop. However, if a different
 * code path is taken on failure (rather than retrying), then the user
 * should be aware that a memory fence might be necessary.
 *
 * Finally, although a local variable isn't strictly needed for the new
 * value, some compilers seem to have trouble when all parameters don't
 * have the same type.
 */
#define atomic_cas(ptr, oval, nval, mo)                             \
MACRO_BEGIN                                                         \
    typeof(*(ptr)) oval_, nval_;                                    \
                                                                    \
    oval_ = (oval);                                                 \
    nval_ = (nval);                                                 \
    __atomic_compare_exchange_n(ptr, &oval_, nval_, false,          \
                                mo, ATOMIC_RELAXED);                \
    oval_;                                                          \

#endif /* atomic_cas */

/*
 * Some architectures may need specific definitions for loads and stores,
 * in order to prevent the compiler from emitting unsupported instructions.
 * As such, only define these if the architecture-specific part of the
 * module didn't already.
 */

#ifndef atomic_load
#define atomic_load(ptr, mo) __atomic_load_n(ptr, mo)
#endif

#ifndef atomic_store
#define atomic_store(ptr, val, mo) __atomic_store_n(ptr, val, mo)
#endif

#define atomic_fence(mo) __atomic_thread_fence(mo)

#endif /* KERN_ATOMIC_H */

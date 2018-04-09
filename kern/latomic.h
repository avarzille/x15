/*
 * Copyright (c) 2018 Richard Braun.
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
 *
 */

#ifndef _KERN_LATOMIC_H
#define _KERN_LATOMIC_H

#include <stdint.h>

#include <kern/macros.h>

#define LATOMIC_RELAXED   __ATOMIC_RELAXED
#define LATOMIC_ACQUIRE   __ATOMIC_ACQUIRE
#define LATOMIC_RELEASE   __ATOMIC_RELEASE
#define LATOMIC_ACQ_REL   __ATOMIC_ACQ_REL
#define LATOMIC_SEQ_CST   __ATOMIC_SEQ_CST

uintptr_t latomic_load_sized(const void *ptr, size_t size);

void latomic_store_sized(void *ptr, const void *val, size_t size);

uintptr_t latomic_swap_sized(void *ptr, const void *val, size_t size);

uintptr_t latomic_cas_sized(void *ptr, const void *oval,
                            const void *nval, size_t size);

uintptr_t latomic_add_sized(void *ptr, const void *val, size_t size);

uintptr_t latomic_and_sized(void *ptr, const void *val, size_t size);

uintptr_t latomic_or_sized(void *ptr, const void *val, size_t size);

uintptr_t latomic_xor_sized(void *ptr, const void *val, size_t size);

#define latomic_load(ptr, mo)   \
  (typeof(*(ptr)))latomic_load_sized(ptr, sizeof(*(ptr)))

#define latomic_store(ptr, val, mo)                      \
MACRO_BEGIN                                              \
    typeof(val) val___;                                  \
                                                         \
    val___ = (val);                                      \
    latomic_store_sized(ptr, &val___, sizeof(val___));   \
MACRO_END

#define latomic_swap(ptr, val, mo)                                      \
MACRO_BEGIN                                                             \
    typeof(val) val___;                                                 \
                                                                        \
    val___ = (val);                                                     \
    (typeof(*(ptr)))latomic_swap_sized(ptr, &val___, sizeof(val___));   \
MACRO_END

#define latomic_cas(ptr, oval, nval, mo)                            \
MACRO_BEGIN                                                         \
    typeof(oval) oval___, nval___;                                  \
                                                                    \
    oval___ = (oval);                                               \
    nval___ = (nval);                                               \
    (typeof(*(ptr)))latomic_cas_sized(ptr, &oval___,                \
                                      &nval___, sizeof(oval___));   \
MACRO_END

#define latomic_fetch_op(ptr, val, op, mo)                                \
MACRO_BEGIN                                                               \
    typeof(val) val___;                                                   \
                                                                          \
    val___ = (val);                                                       \
    (typeof(*(ptr)))latomic_##op##_sized(ptr, &val___, sizeof(val___));   \
MACRO_END

#define latomic_fetch_add(ptr, val, mo)   latomic_fetch_op(ptr, val, add, mo)

#define latomic_fetch_sub(ptr, val, mo)   latomic_fetch_add(ptr, -(val), mo)

#define latomic_fetch_and(ptr, val, mo)   latomic_fetch_op(ptr, val, and, mo)

#define latomic_fetch_or(ptr, val, mo)    latomic_fetch_op(ptr, val, or, mo)

#define latomic_fetch_xor(ptr, val, mo)   latomic_fetch_op(ptr, val, xor, mo)

#define latomic_add   (void)latomic_fetch_add

#define latomic_sub   (void)latomic_fetch_sub

#define latomic_and   (void)latomic_fetch_and

#define latomic_or    (void)latomic_fetch_or

#define latomic_xor   (void)latomic_fetch_xor

#include <machine/latomic.h>

#endif /* _KERN_LATOMIC_H */

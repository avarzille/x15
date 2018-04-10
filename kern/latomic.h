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

#include <kern/macros.h>
#include <machine/cpu.h>
#include <machine/latomic.h>

#define LATOMIC_RELAXED   __ATOMIC_RELAXED
#define LATOMIC_ACQUIRE   __ATOMIC_ACQUIRE
#define LATOMIC_RELEASE   __ATOMIC_RELEASE
#define LATOMIC_ACQ_REL   __ATOMIC_ACQ_REL
#define LATOMIC_SEQ_CST   __ATOMIC_SEQ_CST

#ifndef latomic_load
#define latomic_load(ptr, mo)     \
MACRO_BEGIN                       \
    unsigned long flags___;       \
    uintptr_t ret___;             \
                                  \
    cpu_intr_save(&flags___);     \
    ret___ = (uintptr_t)*(ptr);   \
    cpu_intr_restore(flags___);   \
    (typeof(*ptr))ret___;         \
MACRO_END
#endif /* latomic_load */

#ifndef latomic_store
#define latomic_store(ptr, val, mo)   \
MACRO_BEGIN                           \
    unsigned long flags___;           \
                                      \
    cpu_intr_save(&flags___);         \
    *(ptr) = (val);                   \
    cpu_intr_restore(flags___);       \
    (void)0;                          \
MACRO_END
#endif /* latomic_store */

#ifndef latomic_swap
#define latomic_swap(ptr, val, mo)   \
MACRO_BEGIN                          \
    unsigned long flags___;          \
    typeof(*(ptr)) ret___;           \
                                     \
    cpu_intr_save(&flags___);        \
    ret___ = *(ptr);                 \
    *(ptr) = (val);                  \
    cpu_intr_restore(flags___);      \
    ret___;                          \
MACRO_END
#endif /* latomic_swap */

#ifndef latomic_cas
#define latomic_cas(ptr, oval, nval, mo)   \
MACRO_BEGIN                                \
    unsigned long flags___;                \
    typeof(*(ptr)) ret___;                 \
                                           \
    cpu_intr_save(&flags___);              \
    ret___ = *(ptr);                       \
    if (ret___ == (oval)) {                \
        *(ptr) = (nval);                   \
    }                                      \
    cpu_intr_restore(flags___);            \
    ret___;                                \
MACRO_END
#endif /* latomic_cas */

#define latomic_fetch_op(ptr, val, op, mo)   \
MACRO_BEGIN                                  \
    unsigned long flags___;                  \
    typeof(*(ptr)) ret___;                   \
                                             \
    cpu_intr_save(&flags___);                \
    ret___ = *(ptr);                         \
    *(ptr) op (val);                         \
    cpu_intr_restore(flags___);              \
    ret___;                                  \
MACRO_END

#ifndef latomic_fetch_add
#define latomic_fetch_add(ptr, val, mo)   latomic_fetch_op(ptr, val, +=, mo)
#endif /* latomic_fetch_add */

#ifndef latomic_fetch_sub
#define latomic_fetch_sub(ptr, val, mo)   latomic_fetch_add(ptr, -(val), mo)
#endif /* latomic_fetch_sub */

#ifndef latomic_fetch_and
#define latomic_fetch_and(ptr, val, mo)   latomic_fetch_op(ptr, val, &=, mo)
#endif /* latomic_fetch_and */

#ifndef latomic_fetch_or
#define latomic_fetch_or(ptr, val, mo)    latomic_fetch_op(ptr, val, |=, mo)
#endif /* latomic_fetch_or */

#ifndef latomic_fetch_xor
#define latomic_fetch_xor(ptr, val, mo)   latomic_fetch_op(ptr, val, ^=, mo)
#endif /* latomic_fetch_xor */

#ifndef latomic_add
#define latomic_add   (void)latomic_fetch_add
#endif /* latomic_add */

#ifndef latomic_sub
#define latomic_sub   (void)latomic_fetch_sub
#endif /* latomic_sub */

#ifndef latomic_and
#define latomic_and   (void)latomic_fetch_and
#endif /* latomic_and */

#ifndef latomic_or
#define latomic_or    (void)latomic_fetch_or
#endif /* latomic_or */

#ifndef latomic_xor
#define latomic_xor   (void)latomic_fetch_xor
#endif /* latomic_xor */

#endif /* _KERN_LATOMIC_H */

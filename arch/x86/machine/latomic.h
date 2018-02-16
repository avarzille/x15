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
 *
 * Architecture-specific definitions for local atomics.
 */

#ifndef _X86_LATOMIC_H
#define _X86_LATOMIC_H

#include <stdint.h>

#include <kern/macros.h>

#define LATOMIC_RELAXED   0
#define LATOMIC_ACQUIRE   1
#define LATOMIC_RELEASE   2
#define LATOMIC_ACQ_REL   3
#define LATOMIC_SEQ_CST   4

#ifndef __LP64__

#define latomic_cas_64(ptr, oval, nval)                      \
MACRO_BEGIN                                                  \
    uint64_t ___oval, ___nval;                               \
                                                             \
    ___oval = (oval);                                        \
    ___nval = (nval);                                        \
    __asm__ __volatile__("cmpxchg8b %0\n\t"                  \
                         : "+m" (*(ptr)), "+A" (___oval)     \
                         : "b" ((uint32_t)___nval),          \
                           "c" ((uint32_t)(___nval >> 32))   \
                         : "cc");                            \
    ___oval;                                                 \
MACRO_END

#define latomic_load_64(ptr)   latomic_cas_64(ptr, 0, 0)

#define latomic_swap_64(ptr, val)                                 \
MACRO_BEGIN                                                       \
    uint64_t ___rval, ___val;                                     \
                                                                  \
    ___val = (val);                                               \
    do {                                                          \
        ___rval = *(ptr);                                         \
    } while (latomic_cas_64 (ptr, ___rval, ___val) != ___rval);   \
    ___rval;                                                      \
MACRO_END

#define latomic_cas_loop_64(ptr, op, val)                         \
MACRO_BEGIN                                                       \
    uint64_t ___rval, ___val;                                     \
                                                                  \
    do {                                                          \
        ___rval = *(ptr);                                         \
        ___val = ___rval op (val);                                \
    } while (latomic_cas_64 (ptr, ___rval, ___val) != ___rval);   \
    ___rval;                                                      \
MACRO_END

#define latomic_choose_expr(ptr, expr1, expr2)   \
_Generic((ptr),                                  \
         uint64_t*: expr1,                       \
         int64_t*:  expr1,                       \
         default: expr2)

#else /* __LP64__ */

#define latomic_choose_expr(ptr, expr1, expr2)   (typeof(*(ptr)))(expr2)

#endif /* __LP64__ */

#define latomic_barrier_entry(mo)                                   \
MACRO_BEGIN                                                         \
    if ((mo) != LATOMIC_RELAXED && (mo) != LATOMIC_RELEASE) {       \
        barrier();                                                  \
    }                                                               \
MACRO_END

#define latomic_barrier_exit(mo)                                    \
MACRO_BEGIN                                                         \
    if ((mo) != LATOMIC_RELAXED && (mo) != LATOMIC_ACQUIRE) {       \
        barrier();                                                  \
    }                                                               \
MACRO_END

#define latomic_load(ptr, mo)                                          \
MACRO_BEGIN                                                            \
    typeof(*(ptr)) ___ret;                                             \
                                                                       \
    latomic_barrier_entry(mo);                                         \
    ___ret = latomic_choose_expr(ptr, latomic_load_64(ptr), *(ptr));   \
    latomic_barrier_exit(mo);                                          \
    ___ret;                                                            \
MACRO_END

#define latomic_store(ptr, val, mo)                                         \
MACRO_BEGIN                                                                 \
    latomic_barrier_entry(mo);                                              \
    latomic_choose_expr(ptr, latomic_swap_64(ptr, val), *(ptr) = (val));    \
    latomic_barrier_exit(mo);                                               \
    (void)0;                                                                \
MACRO_END                                                                   \

#define latomic_swap_n(ptr, val)                              \
MACRO_BEGIN                                                   \
    typeof(*(ptr)) ___swap_ret;                               \
                                                              \
    __asm__ __volatile__("xchg %0, %1"                        \
                          : "=r" (___swap_ret)                \
                          : "m" (*ptr), "0" (val));           \
    ___swap_ret;                                              \
MACRO_END

#define latomic_swap(ptr, val, mo)                            \
MACRO_BEGIN                                                   \
    typeof(*(ptr)) ___ret;                                    \
                                                              \
    latomic_barrier_entry(mo);                                \
    ___ret = latomic_choose_expr(ptr,                         \
                                 latomic_swap_64(ptr, val),   \
                                 latomic_swap_n(ptr, val));   \
    latomic_barrier_exit(mo);                                 \
    ___ret;                                                   \
MACRO_END

#define latomic_cas_n(ptr, exp, val)                             \
MACRO_BEGIN                                                      \
    typeof(*(ptr)) ___cas_ret;                                   \
                                                                 \
    __asm__ __volatile__ ("cmpxchg %2, %1"                       \
                          : "=a" (___cas_ret), "=m" (*ptr)       \
                          : "r" (val), "m" (*ptr), "0" (exp));   \
    ___cas_ret;                                                  \
MACRO_END

#define latomic_cas(ptr, oval, nval, mo)                            \
MACRO_BEGIN                                                         \
    typeof(*(ptr)) ___ret;                                          \
                                                                    \
    latomic_barrier_entry(mo);                                      \
    ___ret = latomic_choose_expr(ptr,                               \
                                 latomic_cas_64(ptr, oval, nval),   \
                                 latomic_cas_n(ptr, oval, nval));   \
    latomic_barrier_exit(mo);                                       \
    ___ret;                                                         \
MACRO_END

#define latomic_fetch_add_n(ptr, val)               \
MACRO_BEGIN                                         \
   typeof(*(ptr)) ___add_ret;                       \
                                                    \
   __asm__ __volatile__("xadd %0, %1"               \
                        : "=r" (___add_ret)         \
                        : "m" (*ptr), "0" (val));   \
   ___add_ret;                                      \
MACRO_END

#define latomic_fetch_add(ptr, val, mo)                             \
MACRO_BEGIN                                                         \
   typeof(*(ptr)) ___ret;                                           \
                                                                    \
   latomic_barrier_entry(mo);                                       \
   ___ret = latomic_choose_expr(ptr,                                \
                                latomic_cas_loop_64(ptr, +, val),   \
                                latomic_fetch_add_n(ptr, val));     \
   latomic_barrier_exit(mo);                                        \
   ___ret;                                                          \
MACRO_END

#define latomic_fetch_sub(ptr, val, mo)   latomic_fetch_add(ptr, -(val), mo)

#define latomic_fetch_and_n(ptr, val)                              \
MACRO_BEGIN                                                        \
    typeof(*(ptr)) ___and_ret, ___tmp;                             \
                                                                   \
    do {                                                           \
        ___tmp = *(ptr);                                           \
        ___and_ret = latomic_cas_n(ptr, ___tmp, ___tmp & (val));   \
    } while (___tmp != ___and_ret);                                \
    ___and_ret;                                                    \
MACRO_END                                                          \

#define latomic_fetch_and(ptr, val, mo)                              \
MACRO_BEGIN                                                          \
    typeof(*(ptr)) ___ret;                                           \
                                                                     \
    latomic_barrier_entry(mo);                                       \
    ___ret = latomic_choose_expr(ptr,                                \
                                 latomic_cas_loop_64(ptr, &, val),   \
                                 latomic_fetch_and_n(ptr, val));     \
    latomic_barrier_exit(mo);                                        \
    ___ret;                                                          \
MACRO_END

#define latomic_fetch_or_n(ptr, val)                              \
MACRO_BEGIN                                                       \
    typeof(*(ptr)) ___or_ret, ___tmp;                             \
                                                                  \
    do {                                                          \
        ___tmp = *(ptr);                                          \
        ___or_ret = latomic_cas_n(ptr, ___tmp, ___tmp | (val));   \
    } while (___tmp != ___or_ret);                                \
    ___ret;                                                       \
MACRO_END

#define latomic_fetch_or(ptr, val, mo)                               \
MACRO_BEGIN                                                          \
    typeof(*(ptr)) ___ret;                                           \
                                                                     \
    latomic_barrier_entry(mo);                                       \
    ___ret = latomic_choose_expr(ptr,                                \
                                 latomic_cas_loop_64(ptr, |, val),   \
                                 latomic_fetch_or_n(ptr, val));      \
    latomic_barrier_exit(mo);                                        \
    ___ret;                                                          \
MACRO_END

#define latomic_fetch_xor_n(ptr, val)                             \
MACRO_BEGIN                                                       \
    typeof(*(ptr)) ___or_ret, ___tmp;                             \
                                                                  \
    do {                                                          \
        ___tmp = *(ptr);                                          \
        ___or_ret = latomic_cas_n(ptr, ___tmp, ___tmp ^ (val));   \
    } while (___tmp != ___or_ret);                                \
    ___ret;                                                       \
MACRO_END

#define latomic_fetch_xor(ptr, val, mo)                              \
MACRO_BEGIN                                                          \
    typeof(*(ptr)) ___ret;                                           \
                                                                     \
    latomic_barrier_entry(mo);                                       \
    ___ret = latomic_choose_expr(ptr,                                \
                                 latomic_cas_loop_64(ptr, ^, val),   \
                                 latomic_fetch_xor_n(ptr, val));     \
    latomic_barrier_exit(mo);                                        \
    ___ret;                                                          \
MACRO_END

#endif

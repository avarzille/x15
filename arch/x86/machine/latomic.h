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
#include <machine/latomic_i.h>

#define LATOMIC_RELAXED   0
#define LATOMIC_ACQUIRE   1
#define LATOMIC_RELEASE   2
#define LATOMIC_ACQ_REL   3
#define LATOMIC_SEQ_CST   4

#define latomic_load(ptr, mo)                                                \
MACRO_BEGIN                                                                  \
    typeof(latomic_choose_expr(ptr, latomic_load_64(ptr), *(ptr))) ___ret;   \
                                                                             \
    latomic_barrier_entry(mo);                                               \
    ___ret = latomic_choose_expr(ptr, latomic_load_64(ptr), *(ptr));         \
    latomic_barrier_exit(mo);                                                \
    ___ret;                                                                  \
MACRO_END

#define latomic_store(ptr, val, mo)                                         \
MACRO_BEGIN                                                                 \
    latomic_barrier_entry(mo);                                              \
    latomic_choose_expr(ptr, latomic_swap_64(ptr, val), *(ptr) = (val));    \
    latomic_barrier_exit(mo);                                               \
    (void)0;                                                                \
MACRO_END                                                                   \

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

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
 */

#ifndef _X86_LATOMIC_I_H
#define _X86_LATOMIC_I_H

#ifndef __LP64__

/*
 *On i386, we need a different implementation for 64-bit
 * local atomics, using the 'cmpxchg8b' instruction. For every
 * macro, we need to dispatch based on the type.
 */

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

/*
 * XXX: Note that the following 2 macros use a non-atomic access.
 * This should be fine, however, since we loop until the CAS succeeds,
 * at which point we know there were no data races.
 */

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

/*
 * The first expression refers to a 64-bit value. The second
 * expression is meant to be the 'generic' implementation.
 */
#define latomic_choose_expr(ptr, expr1, expr2)   \
_Generic((ptr),                                  \
         uint64_t*: expr1,                       \
         int64_t*:  expr1,                       \
         default: expr2)

#else /* __LP64__ */

/*
 * On AMD64, we always use the generic.
 */
#define latomic_choose_expr(ptr, expr1, expr2)   (typeof(*(ptr)))(expr2)

#endif /* __LP64__ */

/*
 *For local atomics, memory ordering is implemented by using
 * the 'barrier' macro on entry, exit, both, or neither, according
 * to the specified ordering.
 */
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

/*
 * Type-generic helpers.
 * The LOCK prefix is absent in every single one of them, since these
 * operations are meant to be processor-local, and the 'memory' constraint
 * is too, since the barriers mentioned above will handle that.
 */
#define latomic_swap_n(ptr, val)                              \
MACRO_BEGIN                                                   \
    typeof(*(ptr)) ___swap_ret;                               \
                                                              \
    __asm__ __volatile__("xchg %0, %1"                        \
                          : "=r" (___swap_ret)                \
                          : "m" (*ptr), "0" (val));           \
    ___swap_ret;                                              \
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

#define latomic_fetch_add_n(ptr, val)               \
MACRO_BEGIN                                         \
   typeof(*(ptr)) ___add_ret;                       \
                                                    \
   __asm__ __volatile__("xadd %0, %1"               \
                        : "=r" (___add_ret)         \
                        : "m" (*ptr), "0" (val));   \
   ___add_ret;                                      \
MACRO_END

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

#endif /* _X86_LATOMIC_I_H */

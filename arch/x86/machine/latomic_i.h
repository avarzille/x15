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
 * On i386, a different implementation is needed for 64-bit
 * local atomics, using the 'cmpxchg8b' instruction. For every
 * macro, the type of the pointer is used to dispatch.
 */

#define latomic_cas_64(ptr, oval, nval)                 \
MACRO_BEGIN                                             \
    uint64_t ___oval, ___nval;                          \
    uint64_t *___cas_ptr;                               \
                                                        \
    ___oval = (uint64_t)(oval);                         \
    ___nval = (uint64_t)(nval);                         \
    ___cas_ptr = (uint64_t *)(ptr);                     \
    asm volatile("cmpxchg8b %0"                         \
                 : "+m" (*___cas_ptr), "+A" (___oval)   \
                 : "b" ((uint32_t)___nval),             \
                   "c" ((uint32_t)(___nval >> 32))      \
                 : "cc");                               \
    ___oval;                                            \
MACRO_END

#define latomic_load_64(ptr)   latomic_cas_64((void *)(ptr), 0, 0)

/*
 * XXX: Note that the following 2 macros use a non-atomic access.
 * This should be fine, however, since a loop is performed until
 * the CAS succeeds, at which point it's certain there were no data races.
 */

#define latomic_swap_64(ptr, val)                                    \
MACRO_BEGIN                                                          \
    uint64_t ___val, ___rval;                                        \
    uint64_t *___ptr;                                                \
                                                                     \
    ___ptr = (uint64_t *)(ptr);                                      \
    ___val = (uint64_t)(val);                                        \
    do {                                                             \
        ___rval = *___ptr;                                           \
    } while (latomic_cas_64 (___ptr, ___rval, ___val) != ___rval);   \
    ___rval;                                                         \
MACRO_END

#define latomic_cas_loop_64(ptr, op, val)                           \
MACRO_BEGIN                                                         \
    uint64_t ___val, ___rval;                                       \
    uint64_t *___ptr;                                               \
                                                                    \
    ___ptr = (uint64_t *)(ptr);                                     \
                                                                    \
    do {                                                            \
        ___rval = *___ptr;                                          \
        ___val = ___rval op (val);                                  \
    } while (latomic_cas_64 (___ptr, ___rval, ___val) != ___rval);  \
    ___rval;                                                        \
MACRO_END

/*
 * XXX: Gross hack to shut up annoying warnings from the compiler.
 */
#define latomic_store_64(ptr, val)                             \
MACRO_BEGIN                                                    \
    latomic_swap_64(ptr,                                       \
        ((union { typeof(val) v; uint64_t u; }) { val }).u);   \
MACRO_END                                                      \

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
 * On AMD64, always use the generic.
 */
#define latomic_choose_expr(ptr, expr1, expr2)   (typeof(*(ptr)))(expr2)

#endif /* __LP64__ */

/*
 * For local atomics, memory ordering is implemented by using
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
#define latomic_load_n(ptr)                        \
  latomic_choose_expr(ptr, latomic_load_64(ptr),   \
                      (uintptr_t)__atomic_load_n((ptr), __ATOMIC_RELAXED))

#define latomic_swap_n(ptr, val)             \
MACRO_BEGIN                                  \
    typeof(*(ptr)) ___swap_ret;              \
                                             \
    asm volatile("xchg %0, %1"               \
                 : "=r" (___swap_ret)        \
                 : "m" (*ptr), "0" (val));   \
    ___swap_ret;                             \
MACRO_END

#define latomic_cas_n(ptr, exp, val)                     \
MACRO_BEGIN                                              \
    typeof(*(ptr)) ___cas_ret;                           \
                                                         \
    asm volatile ("cmpxchg %2, %1"                       \
                  : "=a" (___cas_ret), "=m" (*ptr)       \
                  : "r" (val), "m" (*ptr), "0" (exp));   \
    ___cas_ret;                                          \
MACRO_END

#define latomic_fetch_add_n(ptr, val)       \
MACRO_BEGIN                                 \
   typeof(*(ptr)) ___add_ret;               \
                                            \
   asm volatile("xadd %0, %1"               \
                : "=r" (___add_ret)         \
                : "m" (*ptr), "0" (val));   \
   ___add_ret;                              \
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
    ___or_ret;                                                    \
MACRO_END

#define latomic_fetch_xor_n(ptr, val)                              \
MACRO_BEGIN                                                        \
    typeof(*(ptr)) ___xor_ret, ___tmp;                             \
                                                                   \
    do {                                                           \
        ___tmp = *(ptr);                                           \
        ___xor_ret = latomic_cas_n(ptr, ___tmp, ___tmp ^ (val));   \
    } while (___tmp != ___or_ret);                                 \
    ___xor_ret;                                                    \
MACRO_END

#endif /* _X86_LATOMIC_I_H */

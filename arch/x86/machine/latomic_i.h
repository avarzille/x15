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

#ifndef X86_LATOMIC_I_H
#define X86_LATOMIC_I_H

#ifndef __LP64__

/*
 * On i386, a different implementation is needed for 64-bit
 * local atomics, using the 'cmpxchg8b' instruction. For every
 * macro, the type of the pointer is used to dispatch.
 */

#define latomic_cas_64(ptr, oval, nval)                \
MACRO_BEGIN                                            \
    uint64_t oval___, nval___;                         \
    uint64_t *cas_ptr___;                              \
                                                       \
    oval___ = (uint64_t)(oval);                        \
    nval___ = (uint64_t)(nval);                        \
    cas_ptr___ = (uint64_t *)(ptr);                    \
    asm volatile("cmpxchg8b %0"                        \
                 : "+m" (*cas_ptr___), "+A" (oval___)  \
                 : "b" ((uint32_t)nval___),            \
                   "c" ((uint32_t)(nval___ >> 32))     \
                 : "cc");                              \
    oval___;                                           \
MACRO_END

#define latomic_load_64(ptr)   latomic_cas_64((void *)(ptr), 0, 0)

/*
 * XXX: Note that the following 2 macros use a non-atomic access.
 * This should be fine, however, since a loop is performed until
 * the CAS succeeds, at which point it's certain there were no data races.
 */

#define latomic_swap_64(ptr, val)                                  \
MACRO_BEGIN                                                        \
    uint64_t val___, rval___;                                      \
    uint64_t *ptr___;                                              \
                                                                   \
    ptr___ = (uint64_t *)(ptr);                                    \
    val___ = (uint64_t)(val);                                      \
    do {                                                           \
        rval___ = *ptr___;                                         \
    } while (latomic_cas_64 (ptr___, rval___, val___) != rval___); \
    rval___;                                                       \
MACRO_END

#define latomic_cas_loop_64(ptr, op, val)                          \
MACRO_BEGIN                                                        \
    uint64_t val___, rval___;                                      \
    uint64_t *ptr___;                                              \
                                                                   \
    ptr___ = (uint64_t *)(ptr);                                    \
                                                                   \
    do {                                                           \
        rval___ = *ptr___;                                         \
        val___ = rval___ op (val);                                 \
    } while (latomic_cas_64 (ptr___, rval___, val___) != rval___); \
    rval___;                                                       \
MACRO_END

/*
 * XXX: Gross hack to suppress annoying warnings from the compiler.
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
#define latomic_choose_expr(ptr, expr1, expr2) \
_Generic((ptr),                                \
         uint64_t*: expr1,                     \
         int64_t*:  expr1,                     \
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
#define latomic_barrier_entry(mo)                                  \
MACRO_BEGIN                                                        \
    if ((mo) != LATOMIC_RELAXED && (mo) != LATOMIC_RELEASE) {      \
        barrier();                                                 \
    }                                                              \
MACRO_END

#define latomic_barrier_exit(mo)                                   \
MACRO_BEGIN                                                        \
    if ((mo) != LATOMIC_RELAXED && (mo) != LATOMIC_ACQUIRE) {      \
        barrier();                                                 \
    }                                                              \
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

#define latomic_swap_n(ptr, val)               \
MACRO_BEGIN                                    \
    typeof(*(ptr)) swap_ret___;                \
                                               \
    asm volatile("xchg %0, %1"                 \
                 : "=r" (swap_ret___)          \
                 : "m" (*ptr), "0" (val));     \
    swap_ret___;                               \
MACRO_END

#define latomic_cas_n(ptr, exp, val)                       \
MACRO_BEGIN                                                \
    typeof(*(ptr)) cas_ret___;                             \
                                                           \
    asm volatile ("cmpxchg %2, %1"                         \
                  : "=a" (cas_ret___), "=m" (*ptr)         \
                  : "r" (val), "m" (*ptr), "0" (exp));     \
    cas_ret___;                                            \
MACRO_END

#define latomic_fetch_add_n(ptr, val)      \
MACRO_BEGIN                                \
   typeof(*(ptr)) add_ret___;              \
                                           \
   asm volatile("xadd %0, %1"              \
                : "=r" (add_ret___)        \
                : "m" (*ptr), "0" (val));  \
   add_ret___;                             \
MACRO_END

#define latomic_fetch_and_n(ptr, val)                              \
MACRO_BEGIN                                                        \
    typeof(*(ptr)) and_ret___, tmp___;                             \
                                                                   \
    do {                                                           \
        tmp___ = *(ptr);                                           \
        and_ret___ = latomic_cas_n(ptr, tmp___, tmp___ & (val));   \
    } while (tmp___ != and_ret___);                                \
    and_ret___;                                                    \
MACRO_END                                                          \

#define latomic_fetch_or_n(ptr, val)                               \
MACRO_BEGIN                                                        \
    typeof(*(ptr)) or_ret___, tmp___;                              \
                                                                   \
    do {                                                           \
        tmp___ = *(ptr);                                           \
        or_ret___ = latomic_cas_n(ptr, tmp___, tmp___ | (val));    \
    } while (tmp___ != or_ret___);                                 \
    or_ret___;                                                     \
MACRO_END

#define latomic_fetch_xor_n(ptr, val)                              \
MACRO_BEGIN                                                        \
    typeof(*(ptr)) xor_ret___, tmp___;                             \
                                                                   \
    do {                                                           \
        tmp___ = *(ptr);                                           \
        xor_ret___ = latomic_cas_n(ptr, tmp___, tmp___ ^ (val));   \
    } while (tmp___ != xor_ret___);                                \
    xor_ret___;                                                    \
MACRO_END

#endif /* _X86_LATOMIC_I_H */

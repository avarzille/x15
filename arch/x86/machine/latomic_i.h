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
    uint64_t oval_, nval_;                             \
    uint64_t *cas_ptr_;                                \
                                                       \
    oval_ = (uint64_t)(oval);                          \
    nval_ = (uint64_t)(nval);                          \
    cas_ptr_ = (uint64_t *)(ptr);                      \
    asm volatile("cmpxchg8b %0"                        \
                 : "+m" (*cas_ptr_), "+A" (oval_)      \
                 : "b" ((uint32_t)nval_),              \
                   "c" ((uint32_t)(nval_ >> 32))       \
                 : "cc");                              \
    oval_;                                             \
MACRO_END

#define latomic_load_64(ptr)   latomic_cas_64((void *)(ptr), 0, 0)

/*
 * XXX: Note that the following 2 macros use a non-atomic access.
 * This should be fine, however, since a loop is performed until
 * the CAS succeeds, at which point it's certain there were no data races.
 */

#define latomic_swap_64(ptr, val)                                  \
MACRO_BEGIN                                                        \
    uint64_t val_, rval_;                                          \
    uint64_t *ptr_;                                                \
                                                                   \
    ptr_ = (uint64_t *)(ptr);                                      \
    val_ = (uint64_t)(val);                                        \
    do {                                                           \
        rval_ = *ptr_;                                             \
    } while (latomic_cas_64 (ptr_, rval_, val_) != rval_);         \
    rval_;                                                         \
MACRO_END

#define latomic_cas_loop_64(ptr, op, val)                          \
MACRO_BEGIN                                                        \
    uint64_t val_, rval_;                                          \
    uint64_t *ptr_;                                                \
                                                                   \
    ptr_ = (uint64_t *)(ptr);                                      \
                                                                   \
    do {                                                           \
        rval_ = *ptr_;                                             \
        val_ = rval_ op (val);                                     \
    } while (latomic_cas_64 (ptr_, rval_, val_) != rval_);         \
    rval_;                                                         \
MACRO_END

/*
 * XXX: Gross hack to suppress annoying warnings from the compiler.
 */
#define latomic_store_64(ptr, val)     \
MACRO_BEGIN                            \
    union {                            \
        typeof(val) v;                 \
        uint64_t u;                    \
    } tmp_;                            \
                                       \
    tmp_.v = (val);                    \
    latomic_swap_64((ptr), tmp_.u);    \
MACRO_END

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
#define latomic_load_n(ptr)                                    \
  latomic_choose_expr(ptr, latomic_load_64(ptr),               \
                      __atomic_load_n((ptr), __ATOMIC_RELAXED))

#define latomic_swap_n(ptr, val)               \
MACRO_BEGIN                                    \
    typeof(*(ptr)) swap_ret_;                  \
                                               \
    asm volatile("xchg %0, %1"                 \
                 : "=r" (swap_ret_)            \
                 : "m" (*ptr), "0" (val));     \
    swap_ret_;                                 \
MACRO_END

#define latomic_cas_n(ptr, exp, val)                       \
MACRO_BEGIN                                                \
    typeof(*(ptr)) cas_ret_;                               \
                                                           \
    asm volatile ("cmpxchg %2, %1"                         \
                  : "=a" (cas_ret_), "=m" (*ptr)           \
                  : "r" (val), "m" (*ptr), "0" (exp));     \
    cas_ret_;                                              \
MACRO_END

#define latomic_fetch_add_n(ptr, val)      \
MACRO_BEGIN                                \
   typeof(*(ptr)) add_ret_;                \
                                           \
   asm volatile("xadd %0, %1"              \
                : "=r" (add_ret_)          \
                : "m" (*ptr), "0" (val));  \
   add_ret_;                               \
MACRO_END

#define latomic_fetch_and_n(ptr, val)                              \
MACRO_BEGIN                                                        \
    typeof(*(ptr)) and_ret_, tmp_;                                 \
                                                                   \
    do {                                                           \
        tmp_ = *(ptr);                                             \
        and_ret_ = latomic_cas_n(ptr, tmp_, tmp_ & (val));         \
    } while (tmp_ != and_ret_);                                    \
    and_ret_;                                                      \
MACRO_END                                                          \

#define latomic_fetch_or_n(ptr, val)                               \
MACRO_BEGIN                                                        \
    typeof(*(ptr)) or_ret_, tmp_;                                  \
                                                                   \
    do {                                                           \
        tmp_ = *(ptr);                                             \
        or_ret_ = latomic_cas_n(ptr, tmp_, tmp_ | (val));          \
    } while (tmp_ != or_ret_);                                     \
    or_ret_;                                                       \
MACRO_END

#define latomic_fetch_xor_n(ptr, val)                              \
MACRO_BEGIN                                                        \
    typeof(*(ptr)) xor_ret_, tmp_;                                 \
                                                                   \
    do {                                                           \
        tmp_ = *(ptr);                                             \
        xor_ret_ = latomic_cas_n(ptr, tmp_, tmp_ ^ (val));         \
    } while (tmp_ != xor_ret_);                                    \
    xor_ret_;                                                      \
MACRO_END

#endif /* _X86_LATOMIC_I_H */

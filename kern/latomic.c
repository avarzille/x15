#include <kern/latomic.h>
#include <kern/thread.h>

#define latomic_get(ptr, size)   *(const uint##size##_t *)ptr

uintptr_t latomic_load_sized(const void *ptr, size_t size)
{
    uintptr_t ret;
    unsigned long flags;

    thread_preempt_disable_intr_save(&flags);

    switch (size) {
    case sizeof(uint8_t):
        ret = latomic_get(ptr, 8);
        break;
    case sizeof(uint16_t):
        ret = latomic_get(ptr, 16);
        break;
    case sizeof(uint32_t):
        ret = latomic_get(ptr, 32);
        break;
    default:
        ret = latomic_get(ptr, 64);
        break;
    }

    thread_preempt_enable_intr_restore(flags);
    return ret;
}

#define latomic_set_op(ptr, val, size, op)   \
  *(uint##size##_t *)ptr op *(const uint##size##_t *)val

#define latomic_set(ptr, val, size)   latomic_set_op(ptr, val, size, =)

void latomic_store_sized(void *ptr, const void *val, size_t size)
{
    unsigned long flags;

    thread_preempt_disable_intr_save(&flags);

    switch (size) {
    case sizeof(uint8_t):
        latomic_set(ptr, val, 8);
        break;
    case sizeof(uint16_t):
        latomic_set(ptr, val, 16);
        break;
    case sizeof(uint32_t):
        latomic_set(ptr, val, 32);
        break;
    default:
        latomic_set(ptr, val, 64);
        break;
    }

    thread_preempt_enable_intr_restore(flags);
}

uintptr_t latomic_swap_sized(void *ptr, const void *val, size_t size)
{
    uintptr_t ret;
    unsigned long flags;

    thread_preempt_disable_intr_save(&flags);

    switch (size) {
    case sizeof(uint8_t):
        ret = latomic_get(ptr, 8);
        latomic_set(ptr, val, 8);
        break;
    case sizeof(uint16_t):
        ret = latomic_get(ptr, 16);
        latomic_set(ptr, val, 16);
        break;
    case sizeof(uint32_t):
        ret = latomic_get(ptr, 32);
        latomic_set(ptr, val, 32);
        break;
    default:
        ret = latomic_get(ptr, 64);
        latomic_set(ptr, val, 64);
        break;
    }

    thread_preempt_enable_intr_restore(flags);
    return ret;
}

uintptr_t latomic_cas_sized(void *ptr, const void *oval,
                            const void *nval, size_t size)
{
    uintptr_t ret;
    unsigned long flags;

    thread_preempt_disable_intr_save(&flags);

    switch (size) {
    case sizeof(uint8_t):
        ret = latomic_get(ptr, 8);
        if (ret == latomic_get(oval, 8)) {
            latomic_set(ptr, nval, 8);
        }
        break;
    case sizeof(uint16_t):
        ret = latomic_get(ptr, 16);
        if (ret == latomic_get(oval, 16)) {
            latomic_set(ptr, nval, 16);
        }
        break;
    case sizeof(uint32_t):
        ret = latomic_get(ptr, 32);
        if (ret == latomic_get(oval, 32)) {
            latomic_set(ptr, nval, 32);
        }
        break;
    default:
        ret = latomic_get(ptr, 64);
        if (ret == latomic_get(oval, 64)) {
            latomic_set(ptr, nval, 64);
        }
        break;
    }

    thread_preempt_enable_intr_restore(flags);
    return ret;
}

uintptr_t latomic_add_sized(void *ptr, const void *val, size_t size)
{
    uintptr_t ret;
    unsigned long flags;

    thread_preempt_disable_intr_save(&flags);

    switch (size) {
    case sizeof(uint8_t):
        ret = latomic_get(ptr, 8);
        latomic_set_op(ptr, val, 8, +=);
        break;
    case sizeof(uint16_t):
        ret = latomic_get(ptr, 16);
        latomic_set_op(ptr, val, 16, +=);
        break;
    case sizeof(uint32_t):
        ret = latomic_get(ptr, 32);
        latomic_set_op(ptr, val, 32, +=);
        break;
    default:
        ret = latomic_get(ptr, 64);
        latomic_set_op(ptr, val, 64, +=);
        break;
    }

    thread_preempt_enable_intr_restore(flags);
    return ret;
}

uintptr_t latomic_sub_sized(void *ptr, const void *val, size_t size)
{
    uintptr_t ret;
    unsigned long flags;

    thread_preempt_disable_intr_save(&flags);

    switch (size) {
    case sizeof(uint8_t):
        ret = latomic_get(ptr, 8);
        latomic_set_op(ptr, val, 8, -=);
        break;
    case sizeof(uint16_t):
        ret = latomic_get(ptr, 16);
        latomic_set_op(ptr, val, 16, -=);
        break;
    case sizeof(uint32_t):
        ret = latomic_get(ptr, 32);
        latomic_set_op(ptr, val, 32, -=);
        break;
    default:
        ret = latomic_get(ptr, 64);
        latomic_set_op(ptr, val, 64, -=);
        break;
    }

    thread_preempt_enable_intr_restore(flags);
    return ret;
}

uintptr_t latomic_and_sized(void *ptr, const void *val, size_t size)
{
    uintptr_t ret;
    unsigned long flags;

    thread_preempt_disable_intr_save(&flags);

    switch (size) {
    case sizeof(uint8_t):
        ret = latomic_get(ptr, 8);
        latomic_set_op(ptr, val, 8, &=);
        break;
    case sizeof(uint16_t):
        ret = latomic_get(ptr, 16);
        latomic_set_op(ptr, val, 16, &=);
        break;
    case sizeof(uint32_t):
        ret = latomic_get(ptr, 32);
        latomic_set_op(ptr, val, 32, &=);
        break;
    default:
        ret = latomic_get(ptr, 64);
        latomic_set_op(ptr, val, 64, &=);
        break;
    }

    thread_preempt_enable_intr_restore(flags);
    return ret;
}

uintptr_t latomic_or_sized(void *ptr, const void *val, size_t size)
{
    uintptr_t ret;
    unsigned long flags;

    thread_preempt_disable_intr_save(&flags);

    switch (size) {
    case sizeof(uint8_t):
        ret = latomic_get(ptr, 8);
        latomic_set_op(ptr, val, 8, |=);
        break;
    case sizeof(uint16_t):
        ret = latomic_get(ptr, 16);
        latomic_set_op(ptr, val, 16, |=);
        break;
    case sizeof(uint32_t):
        ret = latomic_get(ptr, 32);
        latomic_set_op(ptr, val, 32, |=);
        break;
    default:
        ret = latomic_get(ptr, 64);
        latomic_set_op(ptr, val, 64, |=);
        break;
    }

    thread_preempt_enable_intr_restore(flags);
    return ret;
}

uintptr_t latomic_xor_sized(void *ptr, const void *val, size_t size)
{
    uintptr_t ret;
    unsigned long flags;

    thread_preempt_disable_intr_save(&flags);

    switch (size) {
    case sizeof(uint8_t):
        ret = latomic_get(ptr, 8);
        latomic_set_op(ptr, val, 8, ^=);
        break;
    case sizeof(uint16_t):
        ret = latomic_get(ptr, 16);
        latomic_set_op(ptr, val, 16, ^=);
        break;
    case sizeof(uint32_t):
        ret = latomic_get(ptr, 32);
        latomic_set_op(ptr, val, 32, ^=);
        break;
    default:
        ret = latomic_get(ptr, 64);
        latomic_set_op(ptr, val, 64, ^=);
        break;
    }

    thread_preempt_enable_intr_restore(flags);
    return ret;
}


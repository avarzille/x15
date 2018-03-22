/*
 * Copyright (c) 2012-2018 Richard Braun.
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

#include <kern/strace.h>
#include <kern/symbols.h>
#include <vm/vm_kmem.h>

#ifdef __LP64__
#define STRACE_ADDR_FORMAT "%#018lx"
#else /* __LP64__ */
#define STRACE_ADDR_FORMAT "%#010lx"
#endif /* __LP64__ */

static const char *
strace_lookup(uintptr_t addr, uintptr_t *offset, uintptr_t *size)
{
    struct symbol *s, *end;

    for (s = symbol_table, end = s + symbol_table_size; s != end; ++s) {
        if ((s->size != 0) && (addr >= s->addr)
            && (addr <= s->addr + s->size)) {
            break;
        }
    }

    if ((s >= end) || (s->name == NULL)) {
        return NULL;
    }

    *offset = addr - s->addr;
    *size = s->size;
    return s->name;
}

static void
strace_show_one(unsigned int index, uintptr_t ip)
{
    uintptr_t offset, size;
    const char *name;

    name = strace_lookup(ip, &offset, &size);

    if (name == NULL) {
        printf("#%u [" STRACE_ADDR_FORMAT "]\n", index, ip);
    } else {
        printf("#%u [" STRACE_ADDR_FORMAT "] %s+%#lx/%#lx\n",
               index, ip, name, (unsigned long)offset, (unsigned long)size);
    }
}

void
strace_show(unsigned long ip, unsigned long bp)
{
    phys_addr_t pa;
    void **frame;
    unsigned int i;
    int error;

    strace_show_one(0, ip);

    i = 1;
    frame = (void **)bp;

    for (;;) {
        if (frame == NULL) {
            break;
        }

        error = pmap_kextract((uintptr_t)&frame[1], &pa);

        if (error) {
            printf("strace: unmapped return address at %p\n", &frame[1]);
            break;
        }

        strace_show_one(i, (uintptr_t)frame[1]);
        error = pmap_kextract((uintptr_t)frame, &pa);

        if (error) {
            printf("strace: unmapped frame address at %p\n", frame);
            break;
        }

        i++;
        frame = frame[0];
    }
}


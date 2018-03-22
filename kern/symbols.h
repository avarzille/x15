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

#ifndef _KERN_SYMBOLS_H
#define _KERN_SYMBOLS_H

#include <stdint.h>
#include <kern/macros.h>

struct symbol {
    uintptr_t addr;
    uintptr_t size;
    int type;
    const char *name;
};

#define __symtab __section(".xsymbols")

extern int symbol_table_size __symtab;
extern struct symbol symbol_table[] __symtab;

#endif /* _KERN_SYMBOLS_H */

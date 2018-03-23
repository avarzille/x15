#! /usr/bin/env python3

symtab_size = 0
symtab_elems = []

import sys
for line in sys.stdin:
  line = line.strip ()
  parts = line.split (' ')
  if len (parts) != 4 or parts[3].startswith ("__func__."):
    continue

  symtab_elems.append ("{ 0x%sul, 0x%s, '%s', \"%s\" }" % tuple(parts))
  symtab_size += 1

print ("#include <kern/symbols.h>")
print ("int symbol_table_size __symtab = %d;" % symtab_size)
print ("struct symbol symbol_table[] __symtab = {")

for elem in symtab_elems:
  print ("    " + elem + ",",)

print ("};")

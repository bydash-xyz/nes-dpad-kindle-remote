import sys
from elftools.elf.elffile import ELFFile
from capstone import *
f=ELFFile(open(sys.argv[1],'rb'))
for s in f.get_section_by_name('.dynsym').iter_symbols():
    if s.name==sys.argv[2]:
        a=s['st_value']&~1; n=int(sys.argv[3]) if len(sys.argv)>3 else s['st_size']
        t=f.get_section_by_name('.text'); d=t.data(); ta=t['sh_addr']
        print(s.name, hex(a), s['st_size'])
        for i in Cs(CS_ARCH_ARM,CS_MODE_THUMB).disasm(d[a-ta:a-ta+n],a): print(f"{i.address:x}: {i.mnemonic} {i.op_str}")

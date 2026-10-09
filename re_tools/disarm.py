import sys
from elftools.elf.elffile import ELFFile
from capstone import *
f=ELFFile(open(sys.argv[1],'rb')); a=int(sys.argv[2],16); n=int(sys.argv[3])
t=f.get_section_by_name('.text'); d=t.data(); ta=t['sh_addr']
md=Cs(CS_ARCH_ARM,CS_MODE_ARM)
for i in md.disasm(d[a-ta:a-ta+n],a): print(f"{i.address:x}: {i.mnemonic} {i.op_str}")

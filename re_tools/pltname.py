import sys
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
from capstone import *
from capstone.arm import *
f=ELFFile(open(sys.argv[1],'rb'))
dyn=f.get_section_by_name('.dynsym'); got={}
for s in f.iter_sections():
    if isinstance(s,RelocationSection) and 'plt' in s.name:
        for r in s.iter_relocations(): got[r['r_offset']]=dyn.get_symbol(r['r_info_sym']).name
plt=f.get_section_by_name('.plt'); pd=plt.data(); pa=plt['sh_addr']
md=Cs(CS_ARCH_ARM,CS_MODE_ARM); md.detail=True
for a in sys.argv[2:]:
    a=int(a,16); i=a-pa
    ins=list(md.disasm(pd[i:i+12],a))
    v=a+8+ins[0].operands[2].imm+ins[1].operands[2].imm+ins[2].operands[1].mem.disp
    print(hex(a), got.get(v))

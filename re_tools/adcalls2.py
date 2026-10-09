import sys,bisect
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
from capstone import *
from capstone.arm import *
f=ELFFile(open(sys.argv[1],'rb')); want=sys.argv[2:]
dyn=f.get_section_by_name('.dynsym')
got={}
for s in f.iter_sections():
    if isinstance(s,RelocationSection) and 'plt' in s.name:
        for r in s.iter_relocations():
            got[r['r_offset']]=dyn.get_symbol(r['r_info_sym']).name
plt=f.get_section_by_name('.plt'); pd=plt.data(); pa=plt['sh_addr']
md=Cs(CS_ARCH_ARM,CS_MODE_ARM); md.detail=True
stubs={}
for i in range(0,len(pd)-12,4):
    ins=list(md.disasm(pd[i:i+12],pa+i))
    if len(ins)==3 and ins[0].mnemonic=='add' and ins[0].operands[1].reg==ARM_REG_PC and ins[2].mnemonic=='ldr':
        v=pa+i+8+ins[0].operands[2].imm+ins[1].operands[2].imm+ins[2].operands[1].mem.disp
        if v in got and any(w in got[v] for w in want): stubs[pa+i]=got[v]
print({hex(k):v for k,v in stubs.items()})
syms=sorted((s['st_value']&~1,s.name) for s in dyn.iter_symbols() if s['st_value'] and s['st_info']['type']=='STT_FUNC')
t=f.get_section_by_name('.text'); d=t.data(); ta=t['sh_addr']
md.skipdata=True; win=[]
for ins in md.disasm(d,ta):
    win=(win+[ins])[-5:]
    if ins.id==ARM_INS_BL and ins.operands[0].imm in stubs:
        i=bisect.bisect_right(syms,(ins.address,'~'))-1
        print(hex(ins.address), syms[i][1], '->', stubs[ins.operands[0].imm], '|', ' ; '.join(f"{w.mnemonic} {w.op_str}" for w in win[:-1]))

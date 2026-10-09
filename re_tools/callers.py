import sys, struct
from elftools.elf.elffile import ELFFile
from capstone import *
f=ELFFile(open(sys.argv[1],'rb')); got=int(sys.argv[2],16)
plt=f.get_section_by_name('.plt'); pd=plt.data(); pa=plt['sh_addr']
md=Cs(CS_ARCH_ARM,CS_MODE_ARM); md.detail=True
stub=None
for i in range(0,len(pd)-12,4):
    ins=list(md.disasm(pd[i:i+12],pa+i))
    if len(ins)==3 and ins[0].mnemonic=='add' and 'pc' in ins[0].op_str:
        # add ip,pc,#a ; add ip,ip,#b ; ldr pc,[ip,#c]!
        v=pa+i+8
        for x in ins:
            imm=[o.imm for o in x.operands if o.type==CS_OP_IMM]
            if x.mnemonic=='add' and imm: v+=imm[0]
            if x.mnemonic=='ldr': v+=x.operands[1].mem.disp
        if v==got: stub=pa+i; break
print('stub',hex(stub))
t=f.get_section_by_name('.text'); td=t.data(); ta=t['sh_addr']
mt=Cs(CS_ARCH_ARM,CS_MODE_THUMB); mt.detail=True
calls=[]
for off in range(0,len(td)-4,2):
    for ins in mt.disasm(td[off:off+4],ta+off,1):
        if ins.mnemonic in('blx','bl') and ins.operands[0].type==CS_OP_IMM and ins.operands[0].imm==stub:
            calls.append(ta+off)
print([hex(c) for c in calls])

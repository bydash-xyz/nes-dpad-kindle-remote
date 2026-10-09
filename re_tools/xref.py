import sys, struct
from elftools.elf.elffile import ELFFile
from capstone import *
from capstone.arm import *
f=ELFFile(open(sys.argv[1],'rb'))
segs=[(s['p_vaddr'],s['p_offset'],s['p_filesz']) for s in f.iter_segments() if s['p_type']=='PT_LOAD']
raw=open(sys.argv[1],'rb').read()
def rd(a,n):
    for v,o,sz in segs:
        if v<=a<v+sz: return raw[o+a-v:o+a-v+n]
needles=[s.encode() for s in sys.argv[2:]]
targets={}
for v,o,sz in segs:
    blob=raw[o:o+sz]
    for nd in needles:
        i=blob.find(nd)
        while i!=-1:
            # back up to string start
            j=i
            while j>0 and blob[j-1]!=0: j-=1
            targets[v+j]=blob[j:blob.find(b'\0',j)].decode(errors='replace'); i=blob.find(nd,i+1)
t=f.get_section_by_name('.text'); d=t.data(); ta=t['sh_addr']
md=Cs(CS_ARCH_ARM,CS_MODE_THUMB); md.detail=True; md.skipdata=True
pend={}
for ins in md.disasm(d,ta):
    if ins.id==ARM_INS_LDR and len(ins.operands)==2 and ins.operands[1].type==ARM_OP_MEM and ins.operands[1].mem.base==ARM_REG_PC:
        la=((ins.address+4)&~3)+ins.operands[1].mem.disp
        b=rd(la,4)
        if b: pend[ins.operands[0].reg]=struct.unpack('<I',b)[0]
    elif ins.id==ARM_INS_ADD and len(ins.operands)>=2 and ins.operands[-1].type==ARM_OP_REG and ins.operands[-1].reg==ARM_REG_PC:
        r=ins.operands[0].reg
        if r in pend:
            tgt=(pend.pop(r)+ins.address+4)&0xffffffff
            if tgt in targets: print(hex(ins.address), targets[tgt])

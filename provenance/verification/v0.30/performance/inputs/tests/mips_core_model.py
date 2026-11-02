"""Bounded MIPS core API model with read-only H1 file and heap services.

Loads the exact BDA payload. It never executes application entry, native video,
MXU, audio, firmware scheduling or USB, and cannot establish physical H1 FPS.
Only initial BSS/fixture writes and ABI argument/stack writes are supported by
the acceptance caller; observed gameplay outcomes must come from real APIs.
"""
from pathlib import Path
import struct,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'sdk'))
from h1_bda.resources import PAYLOAD_OFFSET
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_MIPS,UC_MODE_MIPS32,UC_MODE_LITTLE_ENDIAN,UC_HOOK_CODE
from unicorn.mips_const import UC_MIPS_REG_A0,UC_MIPS_REG_A1,UC_MIPS_REG_A2,UC_MIPS_REG_A3,UC_MIPS_REG_RA,UC_MIPS_REG_SP,UC_MIPS_REG_V0,UC_MIPS_REG_PC,UC_MIPS_REG_CP0_STATUS

class CoreMachine:
    def __init__(self,bda,elf,assets):
        with Path(elf).open('rb') as stream:
            symbols=ELFFile(stream).get_section_by_name('.symtab')
            self.symbols={s.name:int(s['st_value']) for s in symbols.iter_symbols()}
            self.symbol_sizes={s.name:int(s['st_size']) for s in symbols.iter_symbols()}
        self.uc=Uc(UC_ARCH_MIPS,UC_MODE_MIPS32|UC_MODE_LITTLE_ENDIAN);self.uc.mem_map(0,64*1024*1024)
        self.uc.reg_write(UC_MIPS_REG_CP0_STATUS,0);self.write(0x83c00020,Path(bda).read_bytes()[PAYLOAD_OFFSET:])
        begin,end=self.symbols['__bss_start'],self.symbols['__bss_end'];self.write(begin,bytes(end-begin))
        self.assets={name:Path(path).read_bytes() for name,path in assets.items()};self.handles={};self.next_handle=0x1020
        self.heap=0x82600000;self.allocations={};self.callbacks={};self.calls=0;self.bytes_read=0
        # Original H1 BDA service-vector slots; unused GUI/input services stay0.
        for slot,table in ((0x83c00004,0x80002000),(0x83c00008,0x80003000),(0x83c0000c,0x80004000),(0x83c00010,0x80005000)):
            self.word(slot,table)
        for table,offset,fn in ((0x80005000,8,self.alloc),(0x80005000,12,self.free),
                                (0x80003000,0,self.file_open),(0x80003000,4,self.file_close),
                                (0x80003000,8,self.file_read),(0x80003000,16,self.file_seek),(0x80003000,20,self.file_tell)):
            address=0x80001000+len(self.callbacks)*32;self.callbacks[address]=fn
            self.write(address,struct.pack('<II',0x03e00008,0));self.word(table+offset,address)
        self.hook=self.uc.hook_add(UC_HOOK_CODE,self.dispatch,begin=0x80001000,end=0x80001fff)
    def read(self,address,count):return bytes(self.uc.mem_read(address&0x1fffffff,count))
    def write(self,address,data):self.uc.mem_write(address&0x1fffffff,data)
    def word(self,address,value):self.write(address,struct.pack('<I',value&0xffffffff))
    def integer(self,address):return struct.unpack('<i',self.read(address,4))[0]
    def string(self,address):
        data=bytearray()
        for i in range(1024):
            char=self.read(address+i,1)
            if char==b'\0':return data.decode('ascii')
            data.extend(char)
        raise AssertionError('Unterminated modeled string')
    def dispatch(self,uc,address,_size,_user):
        assert address in self.callbacks,('Unknown firmware service',hex(address))
        arguments=[uc.reg_read(reg) for reg in (UC_MIPS_REG_A0,UC_MIPS_REG_A1,UC_MIPS_REG_A2,UC_MIPS_REG_A3)]
        arguments.append(struct.unpack('<I',self.read(uc.reg_read(UC_MIPS_REG_SP)+16,4))[0])
        result=self.callbacks[address](*arguments);uc.reg_write(UC_MIPS_REG_V0,(result or 0)&0xffffffff)
        uc.reg_write(UC_MIPS_REG_PC,uc.reg_read(UC_MIPS_REG_RA))
    def alloc(self,size,*_):
        pointer=self.heap;self.heap+=(size+31)&~31;assert self.heap<0x83a00000
        self.allocations[pointer]=size;return pointer
    def free(self,pointer,*_):
        if pointer:assert pointer in self.allocations;self.allocations.pop(pointer)
        return 0
    def file_open(self,path,mode,*_):
        name,access=self.string(path),self.string(mode);assert access=='rb',access
        filename=name.replace('\\','/').rsplit('/',1)[-1].upper()
        if filename not in self.assets:return 0
        handle=self.next_handle;self.next_handle+=4;self.handles[handle]=[filename,0];return handle
    def file_close(self,handle,*_):assert handle in self.handles;self.handles.pop(handle);return 0
    def file_seek(self,handle,offset,whence,*_):
        assert handle in self.handles and whence in(0,1,2);record=self.handles[handle];data=self.assets[record[0]]
        offset=offset if offset<0x80000000 else offset-0x100000000
        position=(0 if whence==0 else record[1] if whence==1 else len(data))+offset
        if position<0 or position>0x7fffffff:return-1
        record[1]=position;return position # H1 seek returns absolute position.
    def file_tell(self,handle,*_):return self.handles[handle][1]
    def file_read(self,dest,size,count,handle,*_):
        assert handle in self.handles
        if not size or not count:return 0
        requested=size*count;assert requested<=1024*1024,requested;record=self.handles[handle];data=self.assets[record[0]]
        got=min(requested,max(0,len(data)-record[1]))//size*size
        if got:self.write(dest,data[record[1]:record[1]+got])
        record[1]+=got;self.bytes_read+=got;return got//size
    def call(self,name,*arguments):
        assert name in self.symbols,('Missing public final ELF API',name)
        context=self.uc.context_save();stack,ret=0x818ff000,0x80001f00;self.write(stack,bytes(64))
        for reg,value in zip((UC_MIPS_REG_A0,UC_MIPS_REG_A1,UC_MIPS_REG_A2,UC_MIPS_REG_A3),arguments):self.uc.reg_write(reg,value&0xffffffff)
        for i,value in enumerate(arguments[4:]):self.word(stack+16+4*i,value)
        self.uc.reg_write(UC_MIPS_REG_SP,stack);self.uc.reg_write(UC_MIPS_REG_RA,ret)
        try:
            self.uc.emu_start(self.symbols[name],ret,timeout=10_000_000,count=200_000_000)
            assert self.uc.reg_read(UC_MIPS_REG_PC)==ret,('MIPS API did not return',name,hex(self.uc.reg_read(UC_MIPS_REG_PC)))
            value=self.uc.reg_read(UC_MIPS_REG_V0);self.calls+=1
        finally:self.uc.context_restore(context)
        return value if value<0x80000000 else value-0x100000000
    def dispose(self):
        if self.uc is not None:self.uc.hook_del(self.hook);self.callbacks.clear();self.uc=None

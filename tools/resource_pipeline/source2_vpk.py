"""Read Valve VPK indexes and extract one explicitly named entry with CRC checks.

Only the requested output file is written. Source archives are opened read-only.
The returned identities describe compiled resource bytes, not decompiled output.
"""
from __future__ import annotations
import argparse
from dataclasses import asdict,dataclass
import fnmatch
import hashlib
import json
from pathlib import Path
import struct
import zlib


@dataclass(frozen=True)
class VpkEntry:
    path:str
    crc32:int
    archive_index:int
    offset:int
    length:int
    preload:bytes

    def description(self)->dict:
        result={k:v for k,v in asdict(self).items() if k!="preload"}
        result["crc32"]=f"{self.crc32:08x}"
        result["preload_bytes"]=len(self.preload)
        result["resource_bytes"]=self.length+len(self.preload)
        return result


class VpkArchive:
    def __init__(self,path:Path|str):
        self.path=Path(path).resolve()
        with self.path.open("rb") as stream:
            header=stream.read(12)
            if len(header)!=12:raise ValueError("Truncated VPK header")
            signature,self.version,self.tree_bytes=struct.unpack("<3I",header)
            if signature!=0x55AA1234 or self.version not in (1,2):raise ValueError("Unsupported VPK signature/version")
            if not 1<=self.tree_bytes<=64*1024*1024:raise ValueError("VPK tree size is out of bounds")
            if self.version==2:
                extra=stream.read(16)
                if len(extra)!=16:raise ValueError("Truncated VPK v2 header")
                self.data_bytes,self.archive_md5_bytes,self.other_md5_bytes,self.signature_bytes=struct.unpack("<4I",extra)
            else:self.data_bytes=None
            self.data_start=stream.tell()+self.tree_bytes
            tree=stream.read(self.tree_bytes)
            if len(tree)!=self.tree_bytes:raise ValueError("Truncated VPK tree")
        self.entries:dict[str,VpkEntry]={}
        cursor=0
        def string()->str:
            nonlocal cursor
            try:end=tree.index(0,cursor)
            except ValueError as exc:raise ValueError("Unterminated VPK name") from exc
            result=tree[cursor:end].decode("utf8");cursor=end+1;return result
        while True:
            extension=string()
            if not extension:break
            while True:
                folder=string()
                if not folder:break
                while True:
                    name=string()
                    if not name:break
                    if cursor+18>len(tree):raise ValueError("Truncated VPK entry")
                    crc,preload,archive,offset,length,terminator=struct.unpack_from("<IHHIIH",tree,cursor);cursor+=18
                    if terminator!=0xffff:raise ValueError("Invalid VPK entry terminator")
                    if cursor+preload>len(tree):raise ValueError("Truncated preload data")
                    payload=tree[cursor:cursor+preload];cursor+=preload
                    path=("" if folder==" " else folder+"/")+name+"."+extension
                    if path in self.entries:raise ValueError(f"Duplicate VPK entry: {path}")
                    self.entries[path]=VpkEntry(path,crc,archive,offset,length,payload)
        self.tree_sha256=hashlib.sha256(tree).hexdigest()

    def read(self,entry:VpkEntry|str)->bytes:
        if isinstance(entry,str):entry=self.entries[entry]
        if entry.length+len(entry.preload)>512*1024*1024:raise ValueError("Resource exceeds the explicit 512 MiB read limit")
        if entry.archive_index==0x7fff:
            source=self.path;offset=self.data_start+entry.offset
            if self.data_bytes is not None and entry.offset+entry.length>self.data_bytes:raise ValueError("Embedded resource exceeds VPK data section")
        else:
            if not self.path.name.endswith("_dir.vpk"):raise ValueError("Split VPK index must have an _dir.vpk suffix")
            source=self.path.with_name(self.path.name[:-8]+f"_{entry.archive_index:03}.vpk")
            offset=entry.offset
        if offset+entry.length>source.stat().st_size:raise ValueError(f"Resource exceeds archive: {entry.path}")
        with source.open("rb") as stream:
            stream.seek(offset);data=entry.preload+stream.read(entry.length)
        if len(data)!=entry.length+len(entry.preload):raise ValueError(f"Truncated resource: {entry.path}")
        actual=zlib.crc32(data)&0xffffffff
        if actual!=entry.crc32:raise ValueError(f"CRC mismatch: {entry.path}: {actual:08x}!={entry.crc32:08x}")
        return data

    def identity(self,entry:VpkEntry|str)->dict:
        if isinstance(entry,str):entry=self.entries[entry]
        data=self.read(entry)
        return {"vpk":str(self.path),"vpk_version":self.version,"vpk_tree_sha256":self.tree_sha256,
                **entry.description(),"sha256":hashlib.sha256(data).hexdigest(),"crc32_verified":True}


def main()->None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vpk",type=Path)
    parser.add_argument("--find",default="*")
    parser.add_argument("--limit",type=int,default=80)
    parser.add_argument("--entry",help="Read and verify this exact case-sensitive resource path")
    parser.add_argument("--output",type=Path,help="Write the one --entry resource to this exact file")
    parser.add_argument("--proof",type=Path,help="Save the JSON identity of --entry")
    parser.add_argument("--overwrite",action="store_true")
    args=parser.parse_args()
    if args.output and not args.entry:parser.error("--output requires --entry")
    archive=VpkArchive(args.vpk)
    if args.entry:
        result=archive.identity(args.entry)
        if args.output:
            output=args.output.resolve()
            if output.exists() and not args.overwrite:parser.error(f"Output already exists: {output}; use --overwrite deliberately")
            output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(archive.read(args.entry))
            result["output"]=str(output)
        if args.proof:
            args.proof.parent.mkdir(parents=True,exist_ok=True)
            args.proof.write_text(json.dumps(result,indent=2)+"\n",encoding="utf8")
    else:
        matches=[entry.description() for name,entry in archive.entries.items() if fnmatch.fnmatchcase(name.lower(),args.find.lower())]
        result={"vpk":str(archive.path),"vpk_version":archive.version,"vpk_tree_sha256":archive.tree_sha256,
                "total_entries":len(archive.entries),"matched_entries":len(matches),"entries":matches[:max(0,args.limit)]}
    print(json.dumps(result,indent=2))


if __name__=="__main__":main()

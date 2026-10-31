"""Restricted read-only parser for VRF's textual KV3 resource dumps.

Supports objects, arrays, scalar values, resource annotations and hexadecimal
byte arrays present in our original particle/material/entity DATA snapshots.
This is an offline parser, not a general binary KV3 implementation.
"""
import json,re
TOKEN=re.compile(r'\s+|//[^\n]*|/\*.*?\*/|""".*?"""|"(?:\\.|[^"\\])*"|[{}\[\]=,:#]|[^\s{}\[\]=,:#]+',re.S)
def parse(text):
    marker=text.find('<!--')
    if marker>=0:
        end=text.find('-->',marker)
        if end<0:raise ValueError('Unterminated KV3 header')
        text=text[end+3:]
    start=text.find('{')
    if start<0:raise ValueError('KV3 object missing')
    tokens=[m[0] for m in TOKEN.finditer(text[start:]) if not m[0].isspace() and not m[0].startswith(('//','/*'))];at=0
    def take(expected=None):
        nonlocal at
        if at>=len(tokens):raise ValueError('Unexpected end of KV3')
        value=tokens[at];at+=1
        if expected is not None and value!=expected:raise ValueError(f'Expected {expected}, got {value}')
        return value
    def value():
        token=take()
        if token=='{':
            out={}
            while tokens[at]!='}':
                if tokens[at]==',':take();continue
                key=take();key=json.loads(key,strict=False) if key.startswith('"') else key
                take('=')
                if key in out:raise ValueError('Duplicate key '+key)
                out[key]=value()
            take('}');return out
        if token=='[':
            out=[]
            while tokens[at]!=']':
                if tokens[at]==',':take();continue
                out.append(value())
            take(']');return out
        if token=='#':
            take('[');out=[]
            while tokens[at]!=']':
                p=take()
                if p!=',':out.append(int(p,16))
            take(']');return {'_blob_hex':bytes(out).hex()}
        if token.startswith('"""'):return token[3:-3]
        if token.startswith('"'):return json.loads(token,strict=False)
        if token in ('true','false'):return token=='true'
        if token in ('null','NULL'):return None
        if at<len(tokens) and tokens[at]==':':take(':');return value()
        try:return int(token,0) if re.fullmatch(r'[-+]?\d+|[-+]?0[xX][0-9a-fA-F]+',token) else float(token)
        except ValueError:return token
    result=value()
    if not isinstance(result,dict):raise ValueError('Expected root object')
    return result

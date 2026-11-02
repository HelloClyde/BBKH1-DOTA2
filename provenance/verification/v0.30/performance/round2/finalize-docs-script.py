"""Apply the prepared acceptance documentation to the confirmed selected build."""
from pathlib import Path
import argparse,json
root=Path.cwd();q=argparse.ArgumentParser(add_help=False);q.add_argument('--profile',type=Path,required=True);a,_=q.parse_known_args();r=json.loads(a.profile.read_bytes());assert r['passed']
builds=[root/'build/v030/perf-candidate6',root/'build/v030/perf-candidate7'];selected=next(p for p in builds if (p/'DOTA2.build.json').exists()and json.loads((p/'DOTA2.build.json').read_bytes())['bda_sha256']==r['bda_sha256']);label='C7'if selected.name.endswith('7')else'C6'
template=root/'build/v030/game-cost-study/finalize-round2-docs.py';text=template.read_text()
if label=='C7':
 text=text.replace('perf-candidate6','perf-candidate7').replace('b79b98ce7d6389b060cc5b586d8631178bc28bea5e886bf74e0e86762d0bd81f',r['bda_sha256']).replace('C6','C7').replace('final-c6','final-c7')
 text=text.replace('1,662,184','1,727,724').replace('1,801,448','1,866,988').replace('98场景/740,553','105场景/801,506').replace('3,211,264','3,440,640').replace('98场景','105场景').replace('host98','host105').replace('guard98','guard105')
 text=text.replace('视野缓存8类只含不可变地形的障碍平面，动态树木另用字节平面；减少每个阴影格反复计算高度舍入、区域及树位提取。','视野缓存8类最终0/1/2障碍平面，双缓冲树木字节平面在树版本变化时比较新旧成员，仅更新改变格且保留terrain-wall2和飞行规则；普通阴影查询直接读一个障碍字节，减少每格高度/区域/树位计算。')
 text=text.replace('C5中间候选的FPS','C5/C6中间候选的FPS')
 text=text.replace('复用8类不可变地形障碍平面及版本化树木字节平面','复用8类最终障碍平面及双缓冲树木字节平面，只在树版本变化时更新变化的非墙格')
 text=text.replace('最终C7新跑视野54','最终C7与隔离CPU原型的BDA字节SHA完全相同；C6→C7相同Game快照预热2tick、测量8tick，Game/迷雾/单位可见性逐字节相同，指令槽减少3.94% / 3.26%。这不换算物理FPS，也不伪称最终ELF再跑CPU。\n\n最终C7新跑视野54')
text=text.replace("copy(Path(__file__).resolve(),'finalize-docs-script.py','archive-script')","copy(template,'finalize-docs-template.py','archive-script');copy(Path(__file__).resolve(),'finalize-docs-script.py','archive-script')")
exec(compile(text,str(template),'exec'))

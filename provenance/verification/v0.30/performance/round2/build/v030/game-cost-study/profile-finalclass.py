from pathlib import Path
p=Path.cwd()/'build/v030/game-cost-study/profile-height-variants.py';text=p.read_text()
text=text.replace('build=OUT/label;out=build/snapshot.stem',"build=(ROOT/'build/v030/perf-candidate6')if label=='baseC6'else OUT/label;out=OUT/label/snapshot.stem")
(p.parent/'baseC6').mkdir(exist_ok=True)
text=text.replace("('base','heightplane','rawlimit')","('baseC6','finalclass')").replace('height-variants-summary.json','finalclass-summary.json');exec(compile(text,str(p),'exec'))

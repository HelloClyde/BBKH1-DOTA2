"""Compare immutable obstruction planes against the exact diagnostic base."""
from pathlib import Path
p=Path.cwd()/'build/v030/game-cost-study/profile-height-variants.py'
text=p.read_text().replace("('base','heightplane','rawlimit')","('base','baseplane4','baseplane8')").replace('height-variants-summary.json','base-plane-summary.json')
exec(compile(text,str(p),'exec'))

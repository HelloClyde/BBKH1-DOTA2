"""Compare exact base8 tree byte variant against the diagnostic base."""
from pathlib import Path
p=Path.cwd()/'build/v030/game-cost-study/profile-height-variants.py';text=p.read_text().replace("('base','heightplane','rawlimit')","('base','base8treebyte')").replace('height-variants-summary.json','treebyte-summary.json');exec(compile(text,str(p),'exec'))

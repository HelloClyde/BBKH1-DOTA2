"""Frozen root-reviewed follow-up selection; default plan, --execute archives."""
from pathlib import Path
import argparse,subprocess,sys
ROOT=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--execute',action='store_true');a=p.parse_args()
args=[
 '--baseline','build/v030/perf3-baseline7-restored-final',
 '--candidate','build/v030/perf3-candidate8-aligned',
 '--candidate-build','build/v030/perf-candidate8',
 '--comparison','build/v030/perf3-hardware-followup/comparison.json',
 '--deployment','build/v030/perf3-candidate8-deploy.json',
 '--tree-report','build/v030/perf3-candidate8-trees-cut-final/verification.json',
 '--final-device-report','build/v030/perf3-final-device/verification.json',
 '--performance-acceptance','passed',
 '--include-root','build/v030/perf3-restored-device',
 '--include-root','build/v030/perf3-candidate8-trees-cut-final',
 '--include-root','build/v030/perf3-final-device',
 '--include-root','build/v030/perf3-hardware-followup/comparison-script-tests',
 '--include-file','build/v030/perf3-candidate8-deploy-tool.json',
 '--include-file','build/v030/perf3-candidate8-deploy.json.upload.json',
 '--include-file','build/v030/perf3-hardware-followup/source-audit.json',
 '--excluded-attempt','build/v030/perf3-baseline7-restored','Driver expected-lineup check rejected this attempt before gameplay comparison. Original failed report is preserved.',
 '--excluded-attempt','build/v030/perf3-baseline7-restored-aligned','Driver expected-lineup check rejected this attempt; no gameplay comparison was performed. Original failed report is preserved.',
 '--excluded-attempt','build/v030/perf3-candidate8-trees-cut','Harness30s startup complete-frame budget expired. Later read-only debug/screenshot showed healthy menu, zero app/resource errors and normal BACK exit; this is excluded as a harness timeout, not a game freeze.',
 '--plan-output','build/v030/perf3-hardware-followup/archive-final-plan.json',
]
if a.execute:args.append('--execute')
raise SystemExit(subprocess.call([sys.executable,str(Path(__file__).with_name('archive_hardware.py')),*args],cwd=ROOT))

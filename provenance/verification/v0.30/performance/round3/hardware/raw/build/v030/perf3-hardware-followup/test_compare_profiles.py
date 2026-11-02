"""Synthetic validator cases, never a new physical candidate measurement."""
from pathlib import Path
import argparse,copy,hashlib,importlib.util,json
ROOT=Path(__file__).resolve().parents[3]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build')and not out.exists();out.mkdir(parents=True)
 driver=Path(__file__).with_name('compare_profiles.py');spec=importlib.util.spec_from_file_location('comparison',driver);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
 base=ROOT/'build/v030/perf2-final7-aligned';build=ROOT/'build/v030/perf-candidate7';original=json.loads((base/'verification.json').read_text());original['fixture_only']=True;candidate=out/'synthetic-input';candidate.mkdir();(candidate/'driver.py').write_bytes((base/'driver.py').read_bytes());cases=[]
 def run(name,change,valid,healthy=True,unknown=False,min_fps=None,threshold=None):
  x=copy.deepcopy(original);x['validator_fixture_case']=name;change(x);(candidate/'verification.json').write_text(json.dumps(x));result=m.compare(base,candidate,build,build,8,.25,min_fps);acceptance=result['acceptance'];assert acceptance['comparison_valid']is valid,(name,acceptance);assert acceptance['candidate_health']is healthy,(name,acceptance);assert bool(result['unknowns'])is unknown;assert acceptance['all_live_windows_at_requested_min_fps']is threshold;assert result['passed']is False and result['physical_h1_tested']is False and result['synthetic_fixture_inputs']is True;cases.append(dict(name=name,passed=True,acceptance=acceptance,unknowns=result['unknowns']))
 run('matching-observations-fixture',lambda x:None,True)
 run('benign-observed-menu-counter-difference',lambda x:x.__setitem__('menu_frame_before_enter',319),True)
 run('mismatched-lineup',lambda x:x['lineup_hero_ids'].reverse(),False)
 run('mismatched-driver',lambda x:x.__setitem__('driver_sha256','f'*64),False)
 run('mismatched-window-order',lambda x:x['profiles'].reverse(),False)
 run('forged-resource-delta',lambda x:x['profiles'][1]['resource_delta'].__setitem__('physical_reads',99999),False)
 run('wrong-fps-arithmetic',lambda x:x['profiles'][1].__setitem__('fps',999),False)
 run('audio-stopped',lambda x:x['profiles'][1]['after']['audio'].__setitem__(1,0),True,False)
 run('resource-cleanup-leak',lambda x:x['after_exit']['resources'].__setitem__('heap_bytes',1),True,False)
 run('missing-audio-remains-unknown',lambda x:x['profiles'][1]['after'].pop('audio'),False,None,True)
 run('missing-cleanup-remains-unknown',lambda x:x.pop('after_exit'),True,None,True)
 run('explicit-unmet-fps-threshold',lambda x:None,True,min_fps=999,threshold=False)
 report=dict(passed=True,fixture_only=True,physical_h1_tested=False,USB_opened=False,comparison_script_sha256=hashlib.sha256(driver.read_bytes()).hexdigest(),test_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),original_report_sha256=hashlib.sha256((base/'verification.json').read_bytes()).hexdigest(),cases=cases);(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(passed=True,cases=len(cases))))
if __name__=='__main__':main()

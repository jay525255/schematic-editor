#!/usr/bin/env python3
"""Collect evidence without converting unexecuted tests into passes."""
import argparse,hashlib,json,os,shutil,subprocess,tarfile
from pathlib import Path
import xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('--platform',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1];out=root/'ci-artifacts';out.mkdir(exist_ok=True);dist=root/'dist';dist.mkdir(exist_ok=True)
sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
def junit(path,expected):
    if not path.is_file():return {'status':'unverified','reason':'result absent'}
    try:
        cases=ET.parse(path).findall('.//testcase');states={}
        for c in cases:
            states[c.get('name')]='not_passed' if c.get('status') in {'notrun','disabled'} or any(c.find(k) is not None for k in ['failure','error','skipped']) else 'passed'
        passed=len(cases)==len(expected) and set(states)==set(expected) and all(x=='passed' for x in states.values())
        return {'status':'passed' if passed else 'not_passed','cases':states}
    except Exception as e:return {'status':'unverified','reason':str(e)}
def external(path):
    if not path.is_file():return {'status':'unverified','reason':'result absent'}
    try:
        r=json.loads(path.read_text());return {'status':'passed' if r.get('verified') is True else 'not_passed','result':r}
    except Exception as e:return {'status':'unverified','reason':str(e)}
url=f"https://github.com/{os.environ.get('GITHUB_REPOSITORY','')}/actions/runs/{os.environ.get('GITHUB_RUN_ID','')}"
status={'source_revision':sha,'platform':a.platform,'workflow_url':url,'run_attempt':os.environ.get('GITHUB_RUN_ATTEMPT'),
 'core':junit(out/'core-junit.xml',['core','simulation','editing_regression','kicad_schematic']),
 'gui':junit(out/'gui/junit.xml',['gui_native']),
 'actual_executable':external(out/'gui/executable/result.json'),'real_kicad_import':external(out/'kicad/result.json')}
if a.platform=='windows':status['note']='GUI and KiCad are assigned to the Ubuntu job; this job does not claim them.'
(out/'status.json').write_text(json.dumps(status,indent=2)+'\n')
products={}
for directory in [root/'build-ci',root/'build/Release']:
    if not directory.exists():continue
    for path in directory.rglob('*'):
        if path.is_file() and (path.suffix in {'.exe','.dll','.pdb'} or (os.name!='nt' and path.name in {'SchematicEditor','schematic-cli','core_tests','simulation_tests','editing_regression_tests','kicad_schematic_tests','gui_native_tests'})):
            if 'CMakeFiles' in path.parts:continue
            target=dist/(directory.name+'-'+path.name);shutil.copy2(path,target);products[target.name]=hashlib.sha256(target.read_bytes()).hexdigest()
subprocess.run(['git','archive','--format=tar.gz','--output='+str(dist/'source.tar.gz'),sha],cwd=root,check=True)
(out/'build-info.json').write_text(json.dumps({'source_revision':sha,'github_sha':os.environ.get('GITHUB_SHA'),'workflow_url':url,'binaries_sha256':products},indent=2)+'\n')
print(json.dumps(status,indent=2))

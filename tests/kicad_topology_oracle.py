#!/usr/bin/env python3
"""Compare imported topology against KiCad 9's independently generated XML netlist."""
import argparse,collections,json,re,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('cli');p.add_argument('fixtures');p.add_argument('output');a=p.parse_args()
out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
fixtures=Path(a.fixtures).resolve();cli=str(Path(a.cli).resolve())
version=subprocess.check_output(['kicad-cli','--version'],text=True).strip()
if not version.startswith('9.'):raise RuntimeError('KiCad 9 required, got '+version)
files=[fixtures/(s+'.kicad_sch') for s in ['rectifier-9','sallen-key-8','antenna-9','mirror-9','pin-to-pin-8']]
# Whole genuine KiCad demo, with only the connector's instance transform changed.
# KiCad itself must accept and export each variant before it can be counted.
base=(fixtures/'antenna-9.kicad_sch').read_text()
start=base.index('\n\t(symbol\n\t\t(lib_id ')
for angle in [0,90,180,270]:
 for mirror in ['', 'x','y']:
  tail=base[start:]
  tail,n=re.subn(r'\(at ([^()\n]+)\)',lambda m:'(at '+' '.join(m.group(1).split()[:2])+f' {angle})'+(f'\n\t\t(mirror {mirror})' if mirror else ''),tail,count=1)
  assert n==1
  path=out/f'antenna-{angle}-{mirror or "normal"}.kicad_sch';path.write_text(base[:start]+tail);files.append(path)

def groups(items):
 result=collections.defaultdict(set)
 for ref,pin,net in items:result[net].add((ref,pin))
 return {frozenset(nodes) for nodes in result.values()}
results=[]
for source in files:
 name=source.stem;xml=out/(name+'.xml');ses=out/(name+'.ses');top=out/(name+'.tsv')
 commands=[['kicad-cli','sch','export','netlist','--format','kicadxml','--output',str(xml),str(source)],
           [cli,'import-kicad',str(source),str(ses)],[cli,'topology',str(ses),str(top)]]
 with (out/(name+'.log')).open('w') as log:
  for cmd in commands:
   log.write('COMMAND '+repr(cmd)+'\n');log.flush();subprocess.run(cmd,check=True,stdout=log,stderr=subprocess.STDOUT,timeout=120)
 root=ET.parse(xml).getroot();expected=[]
 for net in root.findall('./nets/net'):
  for node in net.findall('node'):expected.append((node.attrib['ref'],node.attrib['pin'],net.attrib['code']))
 actual=[line.split('\t') for line in top.read_text().splitlines()]
 eg,ag=groups(expected),groups(actual)
 # KiCad may omit isolated no-connect pins from its netlist. Compare all KiCad
 # exported pins, and independently require every additional editor pin isolated.
 ep={(r,p) for r,p,n in expected};ap={(r,p) for r,p,n in actual}
 if ep-ap:raise AssertionError((name,'missing pins',ep-ap))
 for group in ag:
  if group-ep and len(group)>1:raise AssertionError((name,'unexpected connected pins',group))
 restricted={frozenset(g&ep) for g in ag if g&ep}
 if eg!=restricted:
  raise AssertionError((name,'topology mismatch',{'KiCad only':[sorted(x) for x in eg-restricted],'editor only':[sorted(x) for x in restricted-eg]}))
 results.append({'file':name,'kicad_pins':len(ep),'editor_pins':len(ap),'nets':len(eg),'matched':True})
 print('PASS',name,len(ep),'pins;',len(eg),'nets match KiCad 9',flush=True)
(out/'result.json').write_text(json.dumps({'verified':True,'kicad_version':version,'cases':results},indent=2)+'\n')

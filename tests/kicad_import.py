#!/usr/bin/env python3
"""Import a real exported .net through KiCad's GUI, then inspect KiCad's saved PCB.

Needs the system Python (pcbnew and pyatspi), Xvfb, a D-Bus accessibility session,
xdotool, scrot, KiCad and its Connector_PinHeader_2.54mm footprint library.
No substitute netlist parser creates the board. Missing capabilities are failures,
with verified=false in the evidence, never compatibility passes.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

parser=argparse.ArgumentParser()
parser.add_argument('--cli',required=True,type=Path)
parser.add_argument('--design',required=True,type=Path)
parser.add_argument('--output',required=True,type=Path)
args=parser.parse_args()
out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
result={'verified':False,'method':'real KiCad PCB Editor imports current application export; inspect saved pads',
        'source_revision':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()}
process=None
log_file=None

def event(message,**fields):
    record={'seconds':round(time.monotonic(),3),'event':message,**fields}
    with (out/'events.jsonl').open('a') as f:f.write(json.dumps(record)+'\n')
    print(message,fields,flush=True)

def screenshot(name):
    subprocess.run(['scrot','-o',str(out/name)],check=True,timeout=10)

def walk(root,depth=0):
    yield root
    if depth>16:return
    try:
        for child in root:
            if child is not None:yield from walk(child,depth+1)
    except Exception:return

def wait_for(find,description,timeout=25):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        try:
            value=find()
            if value is not None:return value
        except Exception:pass
        if process is not None and process.poll() is not None:
            raise RuntimeError('KiCad exited before '+description)
        time.sleep(.15)
    raise RuntimeError('Timed out waiting for '+description)

def clean(name):return (name or '').split('\t')[0].replace('&','').replace('_','').strip()

def dump_accessibility():
    # Preserve the real controls available at failure, including hidden submenu
    # items, so differences between distro KiCad/GTK builds are diagnosable.
    try:
        nodes=[]
        for node in walk(pyatspi.Registry.getDesktop(0)):
            try:nodes.append({'name':node.name,'role':node.getRoleName(),'showing':node.getState().contains(pyatspi.STATE_SHOWING)})
            except Exception:continue
        (out/'accessibility.json').write_text(json.dumps(nodes,indent=2)+'\n')
    except Exception:pass

try:
    import pcbnew
    import pyatspi
    for binary in ['pcbnew','xdotool','scrot']:
        if not shutil.which(binary):raise RuntimeError('Missing test dependency: '+binary)
    result['kicad_version']=pcbnew.GetBuildVersion()
    version=re.search(r'(\d+)\.(\d+)',result['kicad_version'])
    if not version:raise RuntimeError('Cannot determine KiCad configuration version')
    footprint_dir=Path('/usr/share/kicad/footprints/Connector_PinHeader_2.54mm.pretty')
    for pin_count in [1,3]:
        if not (footprint_dir/f'PinHeader_1x{pin_count:02d}_P2.54mm_Vertical.kicad_mod').is_file():
            raise RuntimeError('Required real KiCad footprint library is missing')
    netlist=out/'export.net';board_path=out/'imported.kicad_pcb'
    subprocess.run([str(args.cli.resolve()),'export-kicad',str(args.design.resolve()),str(netlist)],check=True,timeout=20)
    # KiCad itself makes a blank board, before the editor imports the .net file.
    if not pcbnew.SaveBoard(str(board_path),pcbnew.BOARD()):raise RuntimeError('Cannot create empty KiCad board')
    with tempfile.TemporaryDirectory(prefix='schematic-kicad-config-') as config:
        config_path=Path(config)/'kicad'/f'{version[1]}.{version[2]}';config_path.mkdir(parents=True)
        table='(fp_lib_table (lib (name "Connector_PinHeader_2.54mm") (type "KiCad") (uri "'+str(footprint_dir)+'") (options "") (descr "CI test footprints")))\n'
        (config_path/'fp-lib-table').write_text(table)
        (config_path/'kicad_common.json').write_text(json.dumps({'meta':{'filename':'kicad_common.json','version':1},'system':{'language':''}}))
        env=dict(os.environ,XDG_CONFIG_HOME=config,LC_ALL='C.UTF-8',LANG='C.UTF-8',NO_AT_BRIDGE='0',LIBGL_ALWAYS_SOFTWARE='1')
        log_file=(out/'pcbnew-process.log').open('w')
        process=subprocess.Popen(['pcbnew',str(board_path)],env=env,stdout=log_file,stderr=subprocess.STDOUT)
        desktop=pyatspi.Registry.getDesktop(0)
        def application():
            for app in desktop:
                try:
                    if app.get_process_id()==process.pid:return app
                except Exception:pass
            return None
        app=wait_for(application,'KiCad accessibility application',45)
        def find(name,roles=None,root=None):
            for node in walk(root or app):
                try:
                    if re.fullmatch(name,clean(node.name),re.I) and (roles is None or node.getRoleName() in roles) and node.getState().contains(pyatspi.STATE_SHOWING):return node
                except Exception:continue
            return None
        def click(node):
            box=node.queryComponent().getExtents(pyatspi.DESKTOP_COORDS)
            if box.width<=0 or box.height<=0:raise RuntimeError('Accessible control has no displayed rectangle')
            subprocess.run(['xdotool','mousemove','--sync',str(box.x+box.width//2),str(box.y+box.height//2),'click','1'],check=True,timeout=10)
            time.sleep(.25)
        def click_named(name,roles=None,root=None,timeout=25):
            node=wait_for(lambda:find(name,roles,root),name,timeout);event('click',control=clean(node.name),role=node.getRoleName());click(node);return node
        # Handle KiCad's optional first-use configuration dialog in this isolated profile.
        time.sleep(2)
        for _ in range(3):
            first_use=find(r'Configure KiCad Settings Path|Configure KiCad Settings',{'dialog','frame'})
            if first_use is None:break
            default=find(r'Start with default settings.*|Use default settings.*',root=first_use)
            if default is not None:click(default)
            click_named(r'OK',{'push button'},first_use);time.sleep(1)
        click_named(r'File',{'menu','menu item'})
        click_named(r'Import',{'menu','menu item'})
        # GTK can close a submenu on a mouse click after pointer hover has
        # already opened it. Explicitly expand the selected Import menu with
        # the native Right key before locating its actual Netlist command.
        subprocess.run(['xdotool','key','Right'],check=True,timeout=10)
        click_named(r'Netlist\.{3}|Netlist…|Import Netlist\.{3}',{'menu item','menu'})
        dialog=wait_for(lambda:find(r'Import Netlist',{'dialog','frame'}),'netlist import dialog')
        def entry():
            for node in walk(dialog):
                try:
                    if node.getRoleName()=='text' and node.getState().contains(pyatspi.STATE_EDITABLE):return node
                except Exception:continue
            return None
        field=wait_for(entry,'netlist filename entry');field.queryEditableText().setTextContents(str(netlist))
        event('set netlist filename',path=netlist.name)
        click_named(r'Load and Test Netlist',{'push button'},dialog)
        time.sleep(2);screenshot('netlist-loaded.png')
        click_named(r'Update PCB',{'push button'},dialog)
        time.sleep(2);screenshot('netlist-updated.png')
        click_named(r'Close',{'push button'},dialog)
        time.sleep(1)
        # End the optional footprint placement command and save the board via native keys.
        subprocess.run(['xdotool','key','Escape','Escape','ctrl+s'],check=True,timeout=10)
        time.sleep(2);screenshot('imported-board.png')
        board=pcbnew.LoadBoard(str(board_path))
        if board is None:raise RuntimeError('KiCad could not reopen its saved board')
        references={};pads={}
        for footprint in board.GetFootprints():
            reference=footprint.GetReference()
            lib_id=footprint.GetFPID()
            references[reference]=str(lib_id.GetLibNickname())+":"+str(lib_id.GetLibItemName())
            for pad in footprint.Pads():pads[reference+'.'+pad.GetNumber()]=pad.GetNetname()
        result['loaded_footprints']=references;result['loaded_pad_nets']=pads
        expected={'IN1.1':'N1','IN2.1':'N2','U3.1':'N1','U3.2':'N2','U3.3':'N3'}
        if set(references)!={'IN1','IN2','U3'}:raise RuntimeError('Imported PCB does not contain exactly the expected three footprints')
        if pads!=expected:raise RuntimeError('Imported PCB pad connectivity differs from exported circuit: '+repr(pads))
        for ref,pin_count in [('IN1',1),('IN2',1),('U3',3)]:
            if references[ref]!=f'Connector_PinHeader_2.54mm:PinHeader_1x{pin_count:02d}_P2.54mm_Vertical':
                raise RuntimeError('KiCad footprint association mismatch: '+ref)
        result['verified']=True;event('PASS real KiCad import, loaded footprints and every pad connection')
except Exception as exc:
    result['error']=str(exc);event('FAIL real KiCad import',error=str(exc))
    dump_accessibility()
    try:screenshot('failure.png')
    except Exception:pass
finally:
    if process is not None and process.poll() is None:
        process.terminate()
        try:process.wait(timeout=5)
        except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5)
    if log_file is not None:log_file.close()
    (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
sys.exit(0 if result['verified'] else 1)

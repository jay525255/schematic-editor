#!/usr/bin/env python3
"""Launch the actual artifact executable, capture its window and close via keyboard."""
import json,os,subprocess,sys,time
from pathlib import Path
binary=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
result={'verified':False,'method':'actual executable, visible X11 window, native Alt+F4'}
p=None
try:
    with (out/'process.log').open('w') as log:
        p=subprocess.Popen([str(binary)],stdout=log,stderr=subprocess.STDOUT)
        deadline=time.monotonic()+20;window=None
        while time.monotonic()<deadline:
            if p.poll() is not None:raise RuntimeError('Executable exited before window appeared')
            found=subprocess.run(['xdotool','search','--onlyvisible','--pid',str(p.pid)],capture_output=True,text=True)
            if found.returncode==0 and found.stdout.strip():window=found.stdout.splitlines()[0];break
            time.sleep(.15)
        if not window:raise RuntimeError('No real visible app window')
        subprocess.run(['xdotool','windowactivate','--sync',window],check=True,timeout=10)
        time.sleep(.8);subprocess.run(['scrot','-u',str(out/'startup.png')],check=True,timeout=10)
        subprocess.run(['xdotool','key','--clearmodifiers','alt+F4'],check=True,timeout=10)
        if p.wait(timeout=10)!=0:raise RuntimeError('Executable exited abnormally')
        result['verified']=True
except Exception as exc:result['error']=str(exc)
finally:
    if p is not None and p.poll() is None:p.terminate()
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result));sys.exit(0 if result['verified'] else 1)

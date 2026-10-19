from pathlib import Path
import json,subprocess,sys
ROOT=Path(__file__).resolve().parent
BUILD=ROOT.parents[1]/'.deps/hsc-native'
def run(command,**kwargs):
 print('RUN '+subprocess.list2cmdline(list(map(str,command))),flush=True)
 subprocess.run(list(map(str,command)),check=True,creationflags=subprocess.CREATE_NO_WINDOW,**kwargs)

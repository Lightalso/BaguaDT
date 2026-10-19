"""Read-only reference fixture export from the supplied reproduction project."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
source=Path('D:/Collection/HSC_reimpl/reproduction/core.py')
spec=importlib.util.spec_from_file_location('hsc_reference',source)
module=importlib.util.module_from_spec(spec)
sys.modules[spec.name]=module
spec.loader.exec_module(module)
data=[]
for stiffness in (1e2,1e4,1e6):
    D,A,E,H,b,x=module.manufactured_system(stiffness=stiffness,nx=2,ny=2,nz=2)
    data.append({'stiffness':stiffness,'femDofs':len(D),'rigidBlocks':[12,12,12],
                 'matrix':H.ravel().tolist(),'rhs':b.tolist(),'expected':x.tolist()})
destination=Path('tests/data/hsc-reference.json')
destination.parent.mkdir(parents=True,exist_ok=True)
destination.write_text(json.dumps({'source':str(source),'sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
                                   'scope':'manufactured SPD validation fixture; not a physical trajectory',
                                   'cases':data},separators=(',',':')),encoding='utf-8')
print(destination)

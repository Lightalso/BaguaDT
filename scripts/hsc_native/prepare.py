from pathlib import Path
import json,subprocess,sys
from prepare_build import BUILD,ROOT,run
for name in ('local_registry.py','json_release_port.py'):
 p=ROOT/name;s=p.read_text(encoding='utf-8').replace("/'source", "/'libuipc").replace("../source/ports","../libuipc/ports");p.write_text(s,encoding='utf-8')
(BUILD/'build').mkdir(exist_ok=True)
(BUILD/'vcpkg_downloads').mkdir(exist_ok=True)
run([sys.executable,BUILD/'libuipc/scripts/gen_vcpkg_json.py',BUILD/'build','--dev_mode=ON','--with_usd_support=OFF','--with_vdb_support=OFF','--with_cuda_backend=ON'])
from local_registry import prepare
prepare()
from json_release_port import apply
apply()
from prefetch_dependencies import prefetch
prefetch()

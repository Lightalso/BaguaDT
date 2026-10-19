from pathlib import Path
import concurrent.futures,hashlib,json,urllib.request,zipfile,shutil
root=Path('.deps/hsc-native').resolve();root.mkdir(parents=True,exist_ok=True)
jobs=[('libuipc','https://codeload.github.com/spiriMirror/libuipc/zip/9c748a759bb9952a7aeedd59076a70591dc61d6c'),('vcpkg','https://codeload.github.com/microsoft/vcpkg/zip/refs/tags/2025.07.25')]
def get(job):
 name,url=job;archive=root/(name+'.zip');target=root/name
 if not archive.exists():
  print('Downloading '+name,flush=True)
  with urllib.request.urlopen(url,timeout=60) as r,archive.with_suffix('.part').open('wb') as f:shutil.copyfileobj(r,f)
  archive.with_suffix('.part').replace(archive)
 if not (target/'.bagua-extracted').exists():
  with zipfile.ZipFile(archive) as z:
   for item in z.infolist():
    parts=Path(item.filename).parts[1:]
    if not parts:continue
    dest=target.joinpath(*parts).resolve()
    if not dest.is_relative_to(target.resolve()):raise RuntimeError('Unsafe archive path')
    if item.is_dir():dest.mkdir(parents=True,exist_ok=True)
    else:
     dest.parent.mkdir(parents=True,exist_ok=True)
     with z.open(item) as src,dest.open('wb') as out:shutil.copyfileobj(src,out)
  (target/'.bagua-extracted').touch()
 rec=dict(name=name,url=url,sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),size=archive.stat().st_size)
 print(json.dumps(rec),flush=True);return rec
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:records=list(pool.map(get,jobs))
(root/'sources.json').write_text(json.dumps(records,indent=2),encoding='utf-8')

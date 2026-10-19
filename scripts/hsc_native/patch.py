"""Apply only application-owned overlays to the isolated pinned SDK; Chrono is untouched."""
from pathlib import Path
import hashlib,json,shutil
from prepare_build import BUILD,ROOT
project=ROOT.parents[1]
source=BUILD/'libuipc'
pristine=BUILD/'pristine'
records=[]
def modify(name,operation):
 p=source/name;backup=pristine/name
 if not backup.exists():
  backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,backup)
 old=backup.read_text(encoding='utf-8');new=operation(old)
 if p.read_text(encoding='utf-8')!=new:p.write_text(new,encoding='utf-8')
 records.append(dict(path=name,before=hashlib.sha256(old.encode()).hexdigest(),after=hashlib.sha256(new.encode()).hexdigest()))
def once(text,old,new):
 if text.count(old)!=1:raise RuntimeError('Pinned source mismatch: '+old[:80])
 return text.replace(old,new)
modify('src/backends/cuda/linear_system/global_linear_system.h',lambda s:once(s,'        CDenseVectorView b() { return m_b; }','        CDenseVectorView b() { return m_b; }\n        CBCOOMatrixView A() const;'))
modify('src/backends/cuda/linear_system/global_linear_system.cu',lambda s:once(s,'REGISTER_SIM_SYSTEM(GlobalLinearSystem);','REGISTER_SIM_SYSTEM(GlobalLinearSystem);\nGlobalLinearSystem::CBCOOMatrixView GlobalLinearSystem::SolvingInfo::A() const { return m_impl->bcoo_A.cview(); }'))
def schema(s):
 s=once(s,'Json::array({"fused_pcg", "linear_pcg"})','Json::array({"fused_pcg", "linear_pcg", "linear_hsc"})')
 entries=''
 for key,value,kind,limits in [('max_rank','IndexT{48}','integer','Json{{"minimum",1}}'),('neumann_order','IndexT{5}','integer','Json{{"minimum",1},{"maximum",32}}'),('max_iter','IndexT{2000}','integer','Json{{"minimum",1}}'),('omega','Float{0.7}','number','Json{{"exclusiveMinimum",0.0},{"maximum",1.0}}'),('adaptive_abd_inverse','IndexT{0}','integer','Json{{"minimum",0},{"maximum",1}}')]:
  entries+=f'    add("linear_system/hsc/{key}", {value}, "{kind}", "BaguaDT HSC {key}", {{"src/backends/cuda/linear_system/linear_hsc.cu"}}, {limits});\n'
 return once(s,'    add("linear_system/fem_preconditioner",',entries+'    add("linear_system/fem_preconditioner",')
modify('src/core/core/scene_default_config.cpp',schema)
def linesearch(s):
 for value in ('energy','total_energy'):
  old=f'!std::isnan({value}) && std::isfinite({value})'
  if old in s:s=s.replace(old,f'!std::isnan({value}) && (std::isfinite({value}) || (!is_initial && {value}>0))')
 return s
modify('src/backends/cuda/line_search/line_searcher.cu',linesearch)
modify('src/backends/cuda/engine/advance_ipc.cu',lambda s:s.replace('if(converged)\n                            break;','if(converged && std::isfinite(E))\n                            break;').replace('constexpr bool AbortOnException = uipc::RUNTIME_CHECK;', 'constexpr bool AbortOnException = false; // Recover solver failures through EngineStatus in the desktop application.'))
for p in (project/'native/overlays').rglob('*'):
 if p.is_file():
  target=source/p.relative_to(project/'native/overlays');target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
shutil.copy2(project/'native/LinearHsc.cu',source/'src/backends/cuda/linear_system/linear_hsc.cu')
for p in [*(project/'native/overlays').rglob('*'),project/'native/LinearHsc.cu']:
 if p.is_file():records.append({'overlay':p.relative_to(project).as_posix(),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
modify('CMakeLists.txt',lambda s:s+f'\nset(BAGUA_SOURCE_DIR "{project.as_posix()}")\nadd_subdirectory("{project.as_posix()}/native" bagua_hsc_native)\n')
(BUILD/'patch-manifest.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
print('Applied complete HSC native overlays to '+str(source),flush=True)

param([switch]$ConfigureOnly,[switch]$BuildOnly)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $taskRoot
$nativeRoot=Join-Path $taskRoot '.deps/hsc-native'
$vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vsRoot=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $vsRoot 'VC/Auxiliary/Build/vcvars64.bat'
$environmentLines=& cmd.exe /d /c "`"$vcvars`" -vcvars_ver=14.50 >nul && set"
foreach($line in $environmentLines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$env:VSLANG='1033'
$env:VCPKG_DISABLE_METRICS='1'
$env:VCPKG_DEFAULT_BINARY_CACHE="$nativeRoot/vcpkg_cache"
$env:VCPKG_DOWNLOADS="$nativeRoot/vcpkg_downloads"
$env:VCPKG_MAX_CONCURRENCY='6'
$env:X_VCPKG_REGISTRIES_CACHE="$nativeRoot/registries"
Remove-Item Env:VCPKG_FORCE_SYSTEM_BINARIES -ErrorAction SilentlyContinue
$env:VCPKG_ROOT="$nativeRoot/vcpkg"
$env:PATH="C:/Program Files/Git/cmd;$env:PATH"
New-Item -ItemType Directory -Force "$nativeRoot/vcpkg_cache","$nativeRoot/registries" | Out-Null
$py='C:/Users/Lightalso/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
function Invoke-Native([string]$exe,[string[]]$parameters){& $exe @parameters;if($LASTEXITCODE -ne 0){throw "$exe failed with $LASTEXITCODE"}}
if($BuildOnly) {
    $cmake=Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    Invoke-Native $cmake @('--build',"$nativeRoot/build",'--parallel','4')
    exit
}
if(!(Test-Path "$nativeRoot/vcpkg/vcpkg.exe")){Invoke-Native 'cmd.exe' @('/d','/c',"$nativeRoot/vcpkg/bootstrap-vcpkg.bat",'-disableMetrics')}
New-Item -ItemType Directory -Force "$nativeRoot/triplets" | Out-Null
@'
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_LOAD_VCVARS_ENV OFF)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/msvc.cmake")
set(VCPKG_ENV_PASSTHROUGH PATH INCLUDE LIB LIBPATH WINDOWSSDKDIR WINDOWSSDKVERSION UCRTVERSION VCPKG_FORCE_SYSTEM_BINARIES)
'@ | Set-Content -Encoding ASCII "$nativeRoot/triplets/x64-windows.cmake"
@'
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER cl.exe)
set(CMAKE_CXX_COMPILER cl.exe)
'@ | Set-Content -Encoding ASCII "$nativeRoot/triplets/msvc.cmake"
Invoke-Native $py @('scripts/hsc_native/prepare.py')
Invoke-Native "$nativeRoot/vcpkg/vcpkg.exe" @('install','--triplet=x64-windows',"--overlay-triplets=$nativeRoot/triplets","--x-manifest-root=$nativeRoot/build","--x-install-root=$nativeRoot/build/vcpkg_installed")
Invoke-Native $py @('scripts/hsc_native/patch.py')
$cmake=Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja=Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$cuda='C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.4'
Invoke-Native $cmake @('-S',"$nativeRoot/libuipc",'-B',"$nativeRoot/build",'-G','Ninja',"-DCMAKE_MAKE_PROGRAM=$ninja",'-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_TOOLCHAIN_FILE=$nativeRoot/vcpkg/scripts/buildsystems/vcpkg.cmake","-DVCPKG_OVERLAY_TRIPLETS=$nativeRoot/triplets",'-DUIPC_BUILD_PYBIND=OFF','-DUIPC_BUILD_EXAMPLES=OFF','-DUIPC_BUILD_TESTS=OFF','-DUIPC_BUILD_BENCHMARKS=OFF','-DUIPC_DEV_MODE=ON','-DUIPC_WITH_CUDA_BACKEND=ON','-DUIPC_WITH_CUDA_LEGACY_COLLISION=ON','-DUIPC_CUDA_ARCHITECTURES=89',"-DCMAKE_CUDA_COMPILER=$cuda/bin/nvcc.exe",'-DCMAKE_CUDA_FLAGS=-Xcompiler=/Zc:__cplusplus -Xcompiler=/bigobj',"-DUIPC_PYTHON_EXECUTABLE_PATH=$py")
if(!$ConfigureOnly){Invoke-Native $cmake @('--build',"$nativeRoot/build",'--parallel','4')}



param(
    [string]$QtRoot = '',
    [string]$ChronoSource = 'D:\Collection\Chrono',
    [string]$EigenRoot = '',
    [string]$CudaRoot = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4',
    [string]$MsvcToolset = '14.50',
    [string]$CudaArchitectures = '89',
    [switch]$EditorOnly,
    [switch]$Sanitize,
    [switch]$Fresh,
    [string]$BuildDirectory = 'build',
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
# MSVC's installed diagnostics are Chinese. UTF-8 makes CMake/Ninja's
# /showIncludes dependency parser agree with cl.exe on the byte encoding.
& cmd.exe /d /c 'chcp 65001 >nul'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$projectRoot = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $projectRoot
if (!$QtRoot) { $QtRoot = Join-Path $projectRoot '.deps\Qt\6.8.3\msvc2022_64' }
if (!$EigenRoot) { $EigenRoot = Join-Path $projectRoot '.deps\eigen-3.4.0' }
$EigenRoot = $EigenRoot.Replace('\','/')
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsRoot) { throw 'MSVC x64 build tools were not found.' }
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$environmentLines = & cmd.exe /d /c "`"$vcvars`" -vcvars_ver=$MsvcToolset >nul && set"
foreach ($line in $environmentLines) { if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') } }
$env:VSLANG = '1033'
$cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
$ninja = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$env:PATH = "$QtRoot\bin;$CudaRoot\bin;$env:PATH"
$env:CUDA_PATH = $CudaRoot
function Invoke-Checked([string]$exe, [string[]]$arguments) { & $exe @arguments; if ($LASTEXITCODE -ne 0) { throw "$exe failed with exit code $LASTEXITCODE" } }
$chronoBuild = Join-Path $projectRoot '.deps\chrono-build'
$chronoInstall = Join-Path $projectRoot '.deps\chrono-install'
if (!$EditorOnly -and !(Test-Path "$chronoInstall\cmake\ChronoConfig.cmake")) {
    Invoke-Checked $cmake @('--fresh','-S',$ChronoSource,'-B',$chronoBuild,'-G','Ninja',"-DCMAKE_MAKE_PROGRAM=$ninja",'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_TRY_COMPILE_CONFIGURATION=Release',"-DEIGEN3_INCLUDE_DIR=$EigenRoot",'-DBUILD_DEMOS=OFF','-DBUILD_TESTING=OFF','-DBUILD_BENCHMARKING=OFF','-DCH_ENABLE_MODULE_IRRLICHT=OFF','-DCH_ENABLE_MODULE_VSG=OFF',"-DCMAKE_INSTALL_PREFIX=$chronoInstall")
    Invoke-Checked $cmake @('--build',$chronoBuild,'--target','install','--parallel','6')
}
$backend = if ($EditorOnly) { 'OFF' } else { 'ON' }
$configure = @('-S','.','-B',$BuildDirectory,'-G','Ninja',"-DCMAKE_MAKE_PROGRAM=$ninja",'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_TRY_COMPILE_CONFIGURATION=Release',"-DCMAKE_PREFIX_PATH=$QtRoot;$chronoInstall", "-DCMAKE_CUDA_COMPILER=$CudaRoot/bin/nvcc.exe", "-DCMAKE_CUDA_ARCHITECTURES=$CudaArchitectures", "-DBAGUA_ENABLE_CUDA=$backend", "-DBAGUA_ENABLE_CHRONO=$backend",'-DBUILD_TESTING=ON')
$configure += "-DBAGUA_EIGEN_INCLUDE=$EigenRoot"
if ($Sanitize) { $configure += '-DBAGUA_SANITIZE=ON' }
$repairDependencies = $false
$cacheRoot = Join-Path $BuildDirectory 'CMakeFiles'
if (Test-Path $cacheRoot) {
    # Old caches created under a different console encoding can miss every header dependency.
    # Refreshing CMake alone cannot repair already stale object files, so clean those as well.
    foreach ($compilerCache in (Get-ChildItem -LiteralPath $cacheRoot -Filter 'CMakeCXXCompiler.cmake' -Recurse)) {
        $cachedCompiler = Get-Content -LiteralPath $compilerCache.FullName -Raw -Encoding UTF8
        if ($cachedCompiler -match 'SHOWINCLUDES_PREFIX[^\r\n]*(娉ㄦ|鍖呭|�)') { $repairDependencies = $true }
    }
}
if ($Fresh -or $repairDependencies) { $configure = @('--fresh') + $configure }
Invoke-Checked $cmake $configure
if ($repairDependencies) {
    Write-Host 'Repairing an obsolete MSVC include-prefix cache; rebuilding all application objects.'
    Invoke-Checked $cmake @('--build',$BuildDirectory,'--target','clean')
}
Invoke-Checked $cmake @('--build',$BuildDirectory,'--parallel','6')
if (Test-Path "$chronoInstall\bin") { Get-ChildItem "$chronoInstall\bin" -Filter '*.dll' | Copy-Item -Destination $BuildDirectory }
$nativeRuntime = Join-Path $projectRoot '.deps\hsc-native\build\Release\bin'
if (!$EditorOnly -and (Test-Path "$nativeRuntime\BaguaHscNative.dll")) {
    Get-ChildItem -LiteralPath $nativeRuntime -Filter '*.dll' | Copy-Item -Destination $BuildDirectory
}
& $ctest --test-dir $BuildDirectory --output-on-failure
$testExit = $LASTEXITCODE
if (Test-Path "$BuildDirectory\test-results.txt") { Get-Content "$BuildDirectory\test-results.txt" }
if ($testExit -ne 0) { throw "Editor tests failed with exit code $testExit" }
if ($Package) {
    New-Item -ItemType Directory -Force dist\BaguaDT | Out-Null
    Copy-Item "$BuildDirectory\BaguaDT.exe" dist\BaguaDT
    Copy-Item "$BuildDirectory\BaguaVehicleRunner.exe" dist\BaguaDT
    New-Item -ItemType Directory -Force dist/BaguaDT/tools | Out-Null
    Copy-Item scripts/ImportHSCVehicle.py dist/BaguaDT/tools
    Get-ChildItem $BuildDirectory -Filter '*.dll' | Copy-Item -Destination dist\BaguaDT
    Invoke-Checked "$QtRoot\bin\windeployqt.exe" @('--release','--no-translations','--no-system-d3d-compiler','dist\BaguaDT\BaguaDT.exe')
    Copy-Item examples dist\BaguaDT -Recurse -Force
    Copy-Item README.md,LICENSE,THIRD_PARTY_NOTICES.md dist\BaguaDT
    Copy-Item scripts\OpenHSC.cmd,scripts\OpenHSCCoupled.cmd,scripts\OpenHSCNative.cmd,scripts\OpenAssembly.cmd,scripts\OpenHSCVehicle.cmd dist\BaguaDT
    Copy-Item docs dist\BaguaDT -Recurse -Force
    New-Item -ItemType Directory -Force dist\BaguaDT\licenses | Out-Null
    Copy-Item licenses\* dist\BaguaDT\licenses -Recurse -Force
    $nativeLicenses=Join-Path $projectRoot '.deps/hsc-native/build/vcpkg_installed/x64-windows/share'
    if(Test-Path $nativeLicenses){
        $noticeFolder=Join-Path $projectRoot 'dist/BaguaDT/licenses/hsc-dependencies'
        New-Item -ItemType Directory -Force $noticeFolder | Out-Null
        Get-ChildItem -LiteralPath $nativeLicenses -Directory | ForEach-Object {
            $copyright=Join-Path $_.FullName 'copyright'
            if(Test-Path $copyright){Copy-Item -LiteralPath $copyright -Destination (Join-Path $noticeFolder ($_.Name+'-copyright.txt'))}
        }
    }
    if (Test-Path "$ChronoSource\LICENSE") { Copy-Item "$ChronoSource\LICENSE" dist\BaguaDT\licenses\Chrono-LICENSE }
    Get-ChildItem $EigenRoot -Filter 'COPYING*' | Copy-Item -Destination dist\BaguaDT\licenses
    if (Test-Path "$QtRoot\licenses") { Copy-Item "$QtRoot\licenses" dist\BaguaDT -Recurse -Force }
}

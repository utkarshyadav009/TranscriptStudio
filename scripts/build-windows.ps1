# Builds Transcript Studio on Windows into dist\Transcript Studio\
#   1. the speech engine (NVIDIA NeMo-Speech.cpp): a CPU flavour, plus a CUDA flavour with -Cuda
#   2. the app itself
#   3. the package: TranscriptStudio.exe, engine\<flavour>\*.dll, models\*.gguf
# Needs: Visual Studio 2022 (C++ workload), CMake, Git; for -Cuda also the CUDA Toolkit 12/13.
param([switch]$Cuda, [string]$CudaArch = "native", [switch]$SkipEngine)
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$ns = Join-Path $root "third_party\NeMo-Speech.cpp"
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$ninja = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

git -C $root submodule update --init third_party/NeMo-Speech.cpp
git -C $ns submodule update --init llama.cpp

# 1. engine
if (-not $SkipEngine) {
    & powershell -ExecutionPolicy Bypass -File "$ns\scripts\windows\build.ps1" -Backend cpu -AsrOnly
    if ($LASTEXITCODE) { throw "CPU engine build failed" }
    if ($Cuda) {
        & powershell -ExecutionPolicy Bypass -File "$ns\scripts\windows\build.ps1" -Backend cuda -AsrOnly -CudaArch $CudaArch -CublasShim
        if ($LASTEXITCODE) { throw "CUDA engine build failed" }
    }
}

# 2. app
cmd /c "call `"$vcvars`" >nul && cd /d `"$root`" && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `"-DCMAKE_MAKE_PROGRAM=$ninja`" && cmake --build build"
if ($LASTEXITCODE) { throw "app build failed" }

# 3. package
& (Join-Path $PSScriptRoot "get-models.ps1")
$dist = Join-Path $root "dist\Transcript Studio"
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force "$dist\models" | Out-Null
Copy-Item "$root\build\TranscriptStudio.exe", "$root\build\ts-cli.exe" $dist
Copy-Item "$root\models\*.gguf" "$dist\models"
foreach ($f in @("cpu") + $(if ($Cuda) { @("cuda") } else { @() })) {
    New-Item -ItemType Directory -Force "$dist\engine\$f" | Out-Null
    Copy-Item "$ns\build-$f-asr\bin\*.dll" "$dist\engine\$f"
}
Copy-Item "$root\THIRD_PARTY_NOTICES.txt" $dist
Write-Host "Built: $dist"

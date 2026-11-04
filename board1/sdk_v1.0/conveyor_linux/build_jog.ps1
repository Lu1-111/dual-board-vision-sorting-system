param([switch]$Board,[switch]$Rtl)
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force -Path build | Out-Null
    $common=@('-std=c++11','-O2','-Wall','-Wextra','-Werror','-I.','conveyor_jog.cpp','conveyor.cpp','uio_backend.cpp','-pthread')
    & 'E:\MinGW\mingw64\bin\g++.exe' @common -o build/conveyor-jog.exe
    if($LASTEXITCODE -ne 0){throw 'Jog CLI build failed'}
    python tests/test_jog_cli.py build/conveyor-jog.exe
    if($LASTEXITCODE -ne 0){throw 'Jog CLI tests failed'}
    if($Rtl){
        python tests/test_jog_rtl.py
        if($LASTEXITCODE -ne 0){throw 'Real CLI to GP0 RTL replay failed'}
    }
    if($Board){
        New-Item -ItemType Directory -Force -Path release | Out-Null
        & 'E:\FD\toolchain\gcc\aarch64_linux\bin\aarch64-linux-gnu-g++.exe' @common -o release/conveyor-jog-aarch64
        if($LASTEXITCODE -ne 0){throw 'Jog AArch64 build failed'}
    }
} finally {Pop-Location}

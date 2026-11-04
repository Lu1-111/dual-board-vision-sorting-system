param(
    [string]$Cxx = 'E:\MinGW\mingw64\bin\g++.exe',
    [switch]$Board,
    [string]$BoardCxx = 'E:\FD\toolchain\gcc\aarch64_linux\bin\aarch64-linux-gnu-g++.exe',
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force -Path build | Out-Null
    $common = @('-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-I.')
    & $Cxx @common conveyorctl.cpp conveyor.cpp uio_backend.cpp -pthread -o build/conveyorctl.exe
    if ($LASTEXITCODE -ne 0) { throw 'CLI build failed' }
    & $Cxx @common tests/test_conveyor.cpp conveyor.cpp uio_backend.cpp -pthread -o build/test_conveyor.exe
    if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
    & ./build/test_conveyor.exe
    if ($LASTEXITCODE -ne 0) { throw 'PS tests failed' }
    & ./build/conveyorctl.exe --simulate --config conveyor.example.ini --demo
    if ($LASTEXITCODE -ne 0) { throw 'Closed-loop demo failed' }
    & ./build/conveyorctl.exe --simulate --config conveyor.cm.simulation.ini --demo
    if ($LASTEXITCODE -ne 0) { throw 'Encoder speed-loop demo failed' }
    & $Python tests/test_cli.py build/conveyorctl.exe
    if ($LASTEXITCODE -ne 0) { throw 'CLI tests failed' }
    & $Python tests/test_calibration.py
    if ($LASTEXITCODE -ne 0) { throw 'Calibration tests failed' }
    if ($Board) {
        New-Item -ItemType Directory -Force -Path release | Out-Null
        & $BoardCxx @common conveyorctl.cpp conveyor.cpp uio_backend.cpp -pthread -o release/conveyorctl-aarch64
        if ($LASTEXITCODE -ne 0) { throw 'AArch64 Linux build failed' }
    }
} finally { Pop-Location }

param([string]$Iverilog = 'E:\Msys2\ucrt64\bin\iverilog.exe',
      [string]$Vvp = 'E:\Msys2\ucrt64\bin\vvp.exe')
$ErrorActionPreference = 'Stop'
$env:PATH = (Split-Path $Iverilog -Parent) + ';' + $env:PATH
$projectDir = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $PSScriptRoot 'build-conveyor'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
$outputFile = Join-Path $buildDir 'conveyor_axi_tb.vvp'
& $Iverilog -g2012 -Wall -s conveyor_axi_tb -o $outputFile (Join-Path $projectDir 'src/conveyor/conveyor_axi.v') (Join-Path $PSScriptRoot 'conveyor_axi_tb.sv')
if ($LASTEXITCODE -ne 0) { throw 'Icarus compilation failed' }
& $Vvp $outputFile
if ($LASTEXITCODE -ne 0) { throw 'RTL simulation failed' }
$routeOutput = Join-Path $buildDir 'conveyor_gp0_tb.vvp'
$rtl = Join-Path $projectDir 'src/conveyor'
& $Iverilog -g2012 -Wall -s conveyor_gp0_tb -o $routeOutput (Join-Path $rtl 'axi_lite_route.v') (Join-Path $rtl 'conveyor_axi.v') (Join-Path $rtl 'conveyor_gp0.v') (Join-Path $PSScriptRoot 'conveyor_gp0_tb.sv')
if ($LASTEXITCODE -ne 0) { throw 'GP0 integration compilation failed' }
& $Vvp $routeOutput
if ($LASTEXITCODE -ne 0) { throw 'GP0 integration simulation failed' }

$externalOutput = Join-Path $buildDir 'conveyor_external_encoder_tb.vvp'
& $Iverilog -g2012 -Wall -s conveyor_gp0_tb '-Pconveyor_gp0_tb.EXTERNAL=1' -o $externalOutput (Join-Path $rtl 'axi_lite_route.v') (Join-Path $rtl 'conveyor_axi.v') (Join-Path $rtl 'conveyor_gp0.v') (Join-Path $PSScriptRoot 'conveyor_gp0_tb.sv')
if ($LASTEXITCODE -ne 0) { throw 'External encoder compilation failed' }
& $Vvp $externalOutput
if ($LASTEXITCODE -ne 0) { throw 'External encoder simulation failed' }

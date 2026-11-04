param([string]$Iverilog='E:\Msys2\ucrt64\bin\iverilog.exe',
      [string]$Vvp='E:\Msys2\ucrt64\bin\vvp.exe')
$ErrorActionPreference='Stop'
$env:PATH=(Split-Path $Iverilog -Parent)+';'+$env:PATH
$projectDir=Split-Path $PSScriptRoot -Parent
$buildDir=Join-Path $PSScriptRoot 'build-control-recovery'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
foreach($case in @(@('pwm_reset_tb','pwm.v','pwm_reset_tb.sv'),
                  @('servo_watchdog_tb','arm_servo_pwm.v','servo_watchdog_tb.sv'),
                  @('arm_servo_pwm_tb','arm_servo_pwm.v','arm_servo_pwm_tb.v'))){
    $output=Join-Path $buildDir ($case[0]+'.vvp')
    & $Iverilog -g2012 -Wall -s $case[0] -o $output (Join-Path $projectDir ('src/'+$case[1])) (Join-Path $PSScriptRoot $case[2])
    if($LASTEXITCODE -ne 0){throw ($case[0]+' compilation failed')}
    & $Vvp $output
    if($LASTEXITCODE -ne 0){throw ($case[0]+' simulation failed')}
}

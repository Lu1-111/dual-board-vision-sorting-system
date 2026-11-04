[CmdletBinding(DefaultParameterSetName = 'GuestScript')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'GuestScript')]
    [string]$GuestScript,

    [Parameter(Mandatory = $true, ParameterSetName = 'Vmrun')]
    [ValidatePattern('^[A-Za-z][A-Za-z0-9]*$')]
    [string]$VmrunCommand,

    [Parameter(ParameterSetName = 'Vmrun')]
    [string[]]$Arguments = @()
)

$ErrorActionPreference = 'Stop'
$vmrun = 'D:\VMware\vmrun.exe'
$vmx = 'D:\uisrc_ubuntu\uisrc-ubuntu18x64\uisrc-ubuntu64x.vmx'
$skillDir = Split-Path -Parent $PSScriptRoot
$credentialPath = Join-Path $skillDir 'credential.xml'

foreach ($path in @($vmrun, $vmx, $credentialPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file is unavailable: $path"
    }
}

$credential = Import-Clixml -LiteralPath $credentialPath
if ($credential -isnot [System.Management.Automation.PSCredential] -or $credential.UserName -ne 'uisrc') {
    throw 'The saved VMware guest credential is invalid.'
}
$guestPassword = $credential.GetNetworkCredential().Password

function Invoke-FixedVmrun {
    param(
        [Parameter(Mandatory = $true)][string]$Command,
        [string[]]$CommandArguments = @()
    )
    & $vmrun -T player -gu 'uisrc' -gp $guestPassword $Command $vmx @CommandArguments
    if ($LASTEXITCODE -ne 0) {
        throw "vmrun $Command failed with exit code $LASTEXITCODE."
    }
}

if ($PSCmdlet.ParameterSetName -eq 'Vmrun') {
    $excluded = @(
        'list', 'listHostNetworks', 'listPortForwardings', 'setPortForwarding',
        'deletePortForwarding', 'downloadPhotonVM', 'clone'
    )
    if ($excluded -contains $VmrunCommand) {
        throw "vmrun command '$VmrunCommand' is outside this single-VM skill."
    }
    Invoke-FixedVmrun -Command $VmrunCommand -CommandArguments $Arguments
    exit 0
}

$id = [guid]::NewGuid().ToString('N')
$guestPrefix = "/tmp/codex-vmrun-$id"
$guestUserScript = "$guestPrefix-user.sh"
$guestWrapper = "$guestPrefix-wrapper.sh"
$guestOutput = "$guestPrefix-output.txt"
$guestStatus = "$guestPrefix-status.txt"
$hostDir = Join-Path ([System.IO.Path]::GetTempPath()) "codex-vmrun-$id"
$hostUserScript = Join-Path $hostDir 'user.sh'
$hostWrapper = Join-Path $hostDir 'wrapper.sh'
$hostOutput = Join-Path $hostDir 'output.txt'
$hostStatus = Join-Path $hostDir 'status.txt'
$utf8 = [System.Text.UTF8Encoding]::new($false)
$resultCode = 1
$resultText = ''

New-Item -ItemType Directory -Path $hostDir | Out-Null
try {
    [System.IO.File]::WriteAllText($hostUserScript, $GuestScript + "`n", $utf8)
    $wrapper = @'
#!/bin/bash
/bin/bash __USER_SCRIPT__ > __OUTPUT__ 2>&1
status=$?
printf '%s\n' "$status" > __STATUS__
exit 0
'@
    $wrapper = $wrapper.Replace('__USER_SCRIPT__', $guestUserScript).
        Replace('__OUTPUT__', $guestOutput).
        Replace('__STATUS__', $guestStatus)
    [System.IO.File]::WriteAllText($hostWrapper, $wrapper + "`n", $utf8)

    Invoke-FixedVmrun -Command 'CopyFileFromHostToGuest' -CommandArguments @($hostUserScript, $guestUserScript)
    Invoke-FixedVmrun -Command 'CopyFileFromHostToGuest' -CommandArguments @($hostWrapper, $guestWrapper)
    Invoke-FixedVmrun -Command 'runProgramInGuest' -CommandArguments @('/bin/bash', $guestWrapper)
    Invoke-FixedVmrun -Command 'CopyFileFromGuestToHost' -CommandArguments @($guestOutput, $hostOutput)
    Invoke-FixedVmrun -Command 'CopyFileFromGuestToHost' -CommandArguments @($guestStatus, $hostStatus)

    $resultText = [System.IO.File]::ReadAllText($hostOutput)
    $statusText = [System.IO.File]::ReadAllText($hostStatus).Trim()
    if (-not [int]::TryParse($statusText, [ref]$resultCode)) {
        throw "Guest script returned an invalid exit code: $statusText"
    }
}
finally {
    foreach ($guestPath in @($guestUserScript, $guestWrapper, $guestOutput, $guestStatus)) {
        & $vmrun -T player -gu 'uisrc' -gp $guestPassword deleteFileInGuest $vmx $guestPath 2>$null | Out-Null
    }
    foreach ($hostPath in @($hostUserScript, $hostWrapper, $hostOutput, $hostStatus)) {
        if (Test-Path -LiteralPath $hostPath -PathType Leaf) {
            Remove-Item -LiteralPath $hostPath -Force
        }
    }
    if (Test-Path -LiteralPath $hostDir -PathType Container) {
        Remove-Item -LiteralPath $hostDir -Force
    }
}

if ($resultText.Length -gt 0) {
    [Console]::Out.Write($resultText)
}
exit $resultCode

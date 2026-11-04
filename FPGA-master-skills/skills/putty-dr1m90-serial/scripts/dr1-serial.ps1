[CmdletBinding(DefaultParameterSetName = 'List')]
param(
    [Parameter(ParameterSetName = 'Open')]
    [switch]$OpenPuTTY,

    [Parameter(ParameterSetName = 'Command', Mandatory = $true)]
    [string]$Command,

    [ValidatePattern('^COM[0-9]+$')]
    [string]$Port,

    [Parameter(ParameterSetName = 'Command')]
    [ValidateRange(100, 30000)]
    [int]$WaitMs = 1000
)

$ports = @(Get-PnpDevice -Class Ports -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object { $_.InstanceId -match '^USB\\VID_1A86&PID_55D2&MI_(00|02)\\' } |
    ForEach-Object {
        if ($_.FriendlyName -match '\((COM[0-9]+)\)') {
            [pscustomobject]@{
                Port = $Matches[1]
                Channel = if ($_.InstanceId -match '&MI_00\\') { 'A (PS console)' } else { 'B' }
                FriendlyName = $_.FriendlyName
                InstanceId = $_.InstanceId
            }
        }
    })

if ($ports.Count -eq 0) {
    throw 'No present CH342 serial port found on Windows. Check USB enumeration or VMware passthrough.'
}

if ($PSCmdlet.ParameterSetName -eq 'List') {
    $ports | Sort-Object Channel, Port
    return
}

if ($Port) {
    $selected = @($ports | Where-Object { $_.Port -eq $Port })
    if ($selected.Count -ne 1) {
        throw "Requested port $Port is not a present CH342 port."
    }
} else {
    $selected = @($ports | Where-Object { $_.Channel -eq 'A (PS console)' })
    if ($selected.Count -ne 1) {
        throw "Expected one CH342 channel A, found $($selected.Count). Pass -Port after identifying the board."
    }
}
$chosen = $selected[0]

if ($PSCmdlet.ParameterSetName -eq 'Open') {
    $putty = Get-Command putty.exe -ErrorAction SilentlyContinue
    if (-not $putty) {
        $candidate = 'C:\Program Files\PuTTY\putty.exe'
        if (Test-Path -LiteralPath $candidate) {
            $putty = [pscustomobject]@{ Source = $candidate }
        } else {
            throw 'PuTTY is not installed or not on PATH.'
        }
    }
    Start-Process -FilePath $putty.Source -ArgumentList @('-serial', $chosen.Port, '-sercfg', '115200,8,n,1,N') -WindowStyle Normal
    Write-Output "Opened PuTTY on $($chosen.Port) ($($chosen.Channel))."
    return
}

$serial = [System.IO.Ports.SerialPort]::new(
    $chosen.Port, 115200, [System.IO.Ports.Parity]::None, 8,
    [System.IO.Ports.StopBits]::One
)
$serial.Handshake = [System.IO.Ports.Handshake]::None
$serial.ReadTimeout = 300
$serial.WriteTimeout = 1000
try {
    $serial.Open()
    $serial.DiscardInBuffer()
    $serial.Write([string][char]13)
    Start-Sleep -Milliseconds 450
    $prompt = $serial.ReadExisting()
    if ($prompt -match 'login:|Password:') {
        throw 'Console is at a login prompt. Log in interactively with PuTTY first.'
    }
    if ($prompt -notmatch '(?m)[#\$]\s*$') {
        throw "No shell prompt detected on $($chosen.Port). Observed: $prompt"
    }
    $serial.Write($Command + [string][char]13)
    Start-Sleep -Milliseconds $WaitMs
    $serial.ReadExisting()
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}

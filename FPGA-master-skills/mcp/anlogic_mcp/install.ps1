param(
    [string]$Python = 'python',
    [string]$Workspace = 'D:\PRJ',
    [string]$TdHome = 'D:\FPGA\TD',
    [string]$FdHome = 'D:\FPGA\FD',
    [switch]$Register
)
$ErrorActionPreference = 'Stop'
$integrationRoot = $PSScriptRoot
$venvPython = Join-Path $integrationRoot '.venv\Scripts\python.exe'
if (!(Test-Path -LiteralPath $venvPython)) {
    & $Python -m venv (Join-Path $integrationRoot '.venv')
    if ($LASTEXITCODE -ne 0) { throw 'Failed to create the Python virtual environment.' }
}
& $venvPython -m pip install --quiet --disable-pip-version-check -r (Join-Path $integrationRoot 'requirements.txt')
if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed.' }
if ($Register) {
    $codexCommand = (Get-Command codex -ErrorAction Stop).Source
    # Older npm CLIs reject the desktop app's service_tier="default".
    # This invocation-only override lets configuration commands parse it;
    # no model is called and the saved service tier is not changed.
    $compatibilityArgs = @('-c', 'service_tier="fast"')
    $serverFile = Join-Path $integrationRoot 'server.py'
    foreach ($toolName in @('td', 'fd')) {
        $serverName = "anlogic-$toolName"
        $existingText = & $codexCommand @compatibilityArgs mcp get $serverName --json 2>$null
        if ($LASTEXITCODE -eq 0) {
            $existing = $existingText | ConvertFrom-Json
            $expectedArgs = @($serverFile, '--tool', $toolName)
            if ($existing.transport.command -ne $venvPython -or
                ($existing.transport.args -join '|') -ne ($expectedArgs -join '|')) {
                throw "$serverName is already configured differently. Inspect it before replacing it."
            }
        }
        & $codexCommand @compatibilityArgs mcp add $serverName `
            --env "ANLOGIC_WORKSPACE=$Workspace" `
            --env "ANLOGIC_TD_HOME=$TdHome" `
            --env "ANLOGIC_FD_HOME=$FdHome" `
            -- $venvPython $serverFile --tool $toolName
        if ($LASTEXITCODE -ne 0) { throw "Registration failed: $serverName" }
    }
}
Write-Host 'Anlogic TD / FD MCP installation complete.'

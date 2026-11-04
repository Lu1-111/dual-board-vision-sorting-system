[CmdletBinding(SupportsShouldProcess = $true)]
param([string]$SkillsRoot = '')
$ErrorActionPreference = 'Stop'
if (-not $SkillsRoot) {
    $codexBase = if ($env:CODEX_HOME) { $env:CODEX_HOME } else { Join-Path ([Environment]::GetFolderPath('UserProfile')) '.codex' }
    $SkillsRoot = Join-Path $codexBase 'skills'
}
$sourceRoot = Join-Path $PSScriptRoot 'skills'
$targetRoot = [IO.Path]::GetFullPath($SkillsRoot)
$toCopy = @()
foreach ($skill in Get-ChildItem -LiteralPath $sourceRoot -Directory) {
    if (-not (Test-Path -LiteralPath (Join-Path $skill.FullName 'SKILL.md') -PathType Leaf)) { continue }
    $destination = Join-Path $targetRoot $skill.Name
    if (Test-Path -LiteralPath $destination) {
        $different = @()
        foreach ($file in Get-ChildItem -LiteralPath $skill.FullName -File -Recurse) {
            $relative = $file.FullName.Substring($skill.FullName.Length + 1)
            $installed = Join-Path $destination $relative
            if (-not (Test-Path -LiteralPath $installed -PathType Leaf)) { $different += $relative; continue }
            if ((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash) { $different += $relative }
        }
        if ($different.Count) { throw "Existing skill differs: $destination. Review these files before manual merge: $($different -join ', ')" }
        Write-Host "Already present, unchanged: $($skill.Name)"
    } else { $toCopy += [pscustomobject]@{ Source = $skill.FullName; Destination = $destination } }
}
foreach ($item in $toCopy) {
    if ($PSCmdlet.ShouldProcess($item.Destination, 'Install skill files')) {
        New-Item -ItemType Directory -Path $targetRoot -Force | Out-Null
        Copy-Item -LiteralPath $item.Source -Destination $item.Destination -Recurse -ErrorAction Stop
    }
}
Write-Host 'Skill installation pass finished. MCP setup is separate; see README.md.'

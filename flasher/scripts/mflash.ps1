# Run several M-Flasher commands under ONE admin prompt instead of one per command.
#   mflash.ps1 "download-mode; info; restart"
#   mflash.ps1 "download-mode; backup C:\path\to\flasher\backups\x; firmware D:\...\fw.fwsc --yes; info; restart"
# Steps run in order and stop at the first one that fails. Output is printed and kept in scripts\mflash.last.log.
# Paths must not contain spaces.
param(
    [Parameter(Mandatory)] [string]$Steps,
    [string]$LogFile
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'tools\m-flasher\M-Flasher.exe'
if (-not $LogFile) { $LogFile = Join-Path $PSScriptRoot 'mflash.last.log' }

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Remove-Item $LogFile -ErrorAction SilentlyContinue
    $p = Start-Process pwsh -Verb RunAs -Wait -PassThru -WindowStyle Hidden -ArgumentList @(
        '-NoProfile', '-File', "`"$PSCommandPath`"", '-Steps', "`"$Steps`"", '-LogFile', "`"$LogFile`"")
    if (Test-Path $LogFile) { Get-Content $LogFile }
    exit $p.ExitCode
}

# Elevated from here on.
$code = 0
foreach ($step in ($Steps -split ';' | ForEach-Object { $_.Trim() } | Where-Object { $_ })) {
    "`n>>> m-flasher $step" | Out-File $LogFile -Append -Encoding utf8
    $stepArgs = @($step -split '\s+')
    & $exe @stepArgs 2>&1 | ForEach-Object { ($_ -replace '(\s+\d{1,3}%)+', '') } | Out-File $LogFile -Append -Encoding utf8
    if ($LASTEXITCODE -ne 0) {
        ">>> stopped: '$step' exited with code $LASTEXITCODE" | Out-File $LogFile -Append -Encoding utf8
        $code = $LASTEXITCODE
        break
    }
    # Give the unit time to re-enumerate after a mode change.
    if ($stepArgs[0] -in 'download-mode', 'restart') { Start-Sleep -Seconds 4 }
}
exit $code

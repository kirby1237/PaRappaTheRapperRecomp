param(
    [ValidateRange(-300,300)][int]$OffsetMs = 0,
    [ValidateRange(0,60)][int]$EarlyMs = 10,
    [ValidateRange(0,60)][int]$LateMs = 10,
    [ValidateRange(30,500)][int]$BufferMs = 60,
    [switch]$Stock,
    [switch]$Hidden,
    [switch]$Direct,
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Profile = 'controller-speakers',
    [string]$Python = 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe'
)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskBuild = Join-Path $taskRoot 'build-timing'
$taskExe = Join-Path $taskBuild 'PaRappaTheRapper_Recompiled.exe'
if (-not (Test-Path -LiteralPath $taskExe)) { throw 'Build build-timing first; see README.' }
$taskProfiles = Join-Path $taskBuild 'timing-profiles'
$taskProfilePath = Join-Path $taskProfiles ($Profile + '.json')
if (Test-Path -LiteralPath $taskProfilePath) {
    $taskProfile = [IO.File]::ReadAllText($taskProfilePath) | ConvertFrom-Json
    if (-not $PSBoundParameters.ContainsKey('OffsetMs')) { $OffsetMs = [int]$taskProfile.OffsetMs }
    if (-not $PSBoundParameters.ContainsKey('EarlyMs')) { $EarlyMs = [int]$taskProfile.EarlyMs }
    if (-not $PSBoundParameters.ContainsKey('LateMs')) { $LateMs = [int]$taskProfile.LateMs }
    if (-not $PSBoundParameters.ContainsKey('BufferMs')) { $BufferMs = [int]$taskProfile.BufferMs }
}
if ($BufferMs -lt 30 -or $BufferMs -gt 500) { throw 'Profile audio buffer must be 30..500 ms.' }
$taskState = Join-Path $taskBuild 'mods\state.toml'
$taskSelection = @((Join-Path $PSScriptRoot 'configure_timing.py'), '--state', $taskState,
    '--early', $EarlyMs, '--late', $LateMs, '--offset', $OffsetMs)
if ($Stock) { $taskSelection += '--stock' }
$taskSelectProfile = $Stock -or -not (Test-Path -LiteralPath $taskState)
foreach ($taskParameter in @('Profile', 'OffsetMs', 'EarlyMs', 'LateMs')) {
    if ($PSBoundParameters.ContainsKey($taskParameter)) { $taskSelectProfile = $true }
}
# Ordinary UI launches keep selections and values saved in the Mods screen.
# Explicit profile/timing arguments deliberately select those settings instead.
if ($taskSelectProfile) {
    & $Python @taskSelection
    if ($LASTEXITCODE -ne 0) { throw 'Timing selection failed.' }
}
New-Item -ItemType Directory -Path $taskProfiles -Force | Out-Null
$taskProfileJson = @{OffsetMs=$OffsetMs; EarlyMs=$EarlyMs; LateMs=$LateMs; BufferMs=$BufferMs} | ConvertTo-Json
[IO.File]::WriteAllText($taskProfilePath, $taskProfileJson, (New-Object Text.UTF8Encoding($false)))
$taskConfig = [IO.File]::ReadAllText((Join-Path $taskRoot 'game.toml'))
$taskRootForward = $taskRoot.Replace('\','/')
$taskConfig = $taskConfig -replace '(?m)^exe = .*$', ('exe = "' + $taskRootForward + '/disc/SCUS_941.83"')
$taskConfig = $taskConfig -replace '(?m)^disc = .*$', ('disc = "' + $taskRootForward + '/disc/PaRappa the Rapper.cue"')
$taskConfig += "`n[audio]`nbuffer_ms = $BufferMs`n"
$taskConfigPath = Join-Path $taskBuild 'game-play.toml'
[IO.File]::WriteAllText($taskConfigPath, $taskConfig, (New-Object Text.UTF8Encoding($false)))
$taskArgs = @('--game', ('"' + $taskConfigPath + '"'), '--debug-port', '9453',
    '--memcard-dir', 'timing-saves')
if ($Direct -or $Hidden) { $taskArgs += '--no-launcher' }
else { $taskArgs += '--launcher' }
if ($Hidden) { $taskArgs += '--hidden-window' }
$taskProcess = Start-Process -FilePath $taskExe -ArgumentList $taskArgs -WorkingDirectory $taskBuild `
    -WindowStyle Hidden -PassThru
Write-Output ('Started isolated kirby1237 timing build, PID ' + $taskProcess.Id)

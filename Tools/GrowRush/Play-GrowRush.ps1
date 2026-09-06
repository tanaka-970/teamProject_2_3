param([switch]$NoBuild, [switch]$Test, [ValidateSet('', 'title', 'arena', 'result')][string]$Capture = '')
$ErrorActionPreference = 'Stop'
function Write-GrowUtf8([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding($false)))
}
$growRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Set-Location -LiteralPath $growRoot
if (-not $NoBuild) {
    & dotnet build 'Scripts/RePlayGameScripts.csproj' -c Release --no-restore
    if ($LASTEXITCODE -ne 0) { throw 'C# build failed.' }
}
$growOutput = Join-Path $growRoot $(if ($Test) { 'Saved/GrowRushValidation' } else { 'Saved/GrowRush' })
New-Item -ItemType Directory -Force -Path $growOutput | Out-Null
$growEngine = Join-Path $growRoot 'x64/Release/3dgp.exe'
if (-not (Test-Path -LiteralPath $growEngine)) { throw 'Build the engine in Release x64 first.' }
Copy-Item -LiteralPath $growEngine -Destination (Join-Path $growOutput 'GrowRush.exe') -Force
Get-ChildItem -LiteralPath (Split-Path $growEngine) -Filter '*.dll' | Copy-Item -Destination $growOutput -Force
# A game-specific content root avoids inheriting the editor project's loading scene and rendering settings.
foreach ($growRelative in @('resources/Game/GrowRush', 'Shader', 'ThirdParty/DXC/bin/x64', 'Managed/RePlayEngine.Managed/bin/Release/net8.0', 'Scripts/bin/Release/net8.0')) {
    $growSource = Join-Path $growRoot $growRelative
    $growDestination = Join-Path $growOutput $growRelative
    New-Item -ItemType Directory -Force -Path $growDestination | Out-Null
    Get-ChildItem -LiteralPath $growSource | Copy-Item -Destination $growDestination -Recurse -Force
}
$growDatabase = @('REPLAY_ASSET_DB 2 4')
foreach ($growLine in Get-Content -LiteralPath (Join-Path $growRoot 'resources/AssetDatabase.replaydb')) {
    if ($growLine -match '^"beea44001a1543a2bfe880773240000[1234]"') { $growDatabase += $growLine }
}
Write-GrowUtf8 (Join-Path $growOutput 'resources/AssetDatabase.replaydb') (($growDatabase -join "`n") + "`n")
$growProject = @'
REPLAY_PROJECT 10
STARTUP_SCENE "beea44001a1543a2bfe8807732400001"
LOADING_SCENE ""
SCENE_FLOW "beea44001a1543a2bfe8807732400004"
DEFAULT_LANGUAGE "ja"
RENDER_SSAO 0
RENDER_SSR 0
RENDER_TAA 0
RENDER_LUMINANCE_ENABLED 0
RENDER_FINAL_PASS_ENABLED 1
UI_FOCUS_OUTLINE_ENABLED 1
'@
Write-GrowUtf8 (Join-Path $growOutput 'resources/Project.replayproject') $growProject
$growGame = @'
REPLAY_GAME 1
NAME GROW RUSH
STARTUP_SCENE resources/Game/GrowRush/GrowRush_Title.replayscene
WINDOW 1600 900
FULLSCREEN 0
'@
Write-GrowUtf8 (Join-Path $growOutput 'GrowRush.replaygame') $growGame
$env:GROWRUSH_TEST = if ($Test) { '1' } else { '' }
$env:GROWRUSH_CAPTURE = $Capture
$growArguments = @('--game')
if ($Capture) { $growArguments = @('--smoke-test', '3600', '--game', '--capture-frame', ('growrush_' + $Capture)) }
if ($Test) {
    $growLog = Join-Path $growOutput 'Saved/GrowRush/session.log'
    if (Test-Path -LiteralPath $growLog) { Clear-Content -LiteralPath $growLog }
    # Profiling supplies fixed time and waits for startup readiness before counting frames.
    $growArguments = @('--profile-scene', 'resources/Game/GrowRush/GrowRush_Title.replayscene', '--frames', '4000', '--warmup', '0', '--out', 'growrush-loop')
    $growProcess = Start-Process -FilePath (Join-Path $growOutput 'GrowRush.exe') -WorkingDirectory $growOutput -ArgumentList $growArguments -WindowStyle Hidden -RedirectStandardOutput (Join-Path $growOutput 'stdout.log') -RedirectStandardError (Join-Path $growOutput 'stderr.log') -PassThru
} elseif ($Capture) {
    $growProcess = Start-Process -FilePath (Join-Path $growOutput 'GrowRush.exe') -WorkingDirectory $growOutput -ArgumentList $growArguments -WindowStyle Hidden -PassThru
} else {
    # This is the user-facing game launcher, so its game window is intentionally visible.
    $growProcess = Start-Process -FilePath (Join-Path $growOutput 'GrowRush.exe') -WorkingDirectory $growOutput -ArgumentList $growArguments -PassThru
}
Write-Output "Grow Rush PID: $($growProcess.Id)"

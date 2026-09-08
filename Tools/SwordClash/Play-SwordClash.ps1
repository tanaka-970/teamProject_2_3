param([switch]$NoBuild, [switch]$Headless, [int]$Frames = 0)
# SWORD CLASH を単体 EXE として組み立てて起動する。
# エディターのプロジェクト設定を持ち込まないよう、専用の content root を作る。
$ErrorActionPreference = 'Stop'

function Write-Utf8([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding($false)))
}

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Set-Location -LiteralPath $root

if (-not $NoBuild) {
    & python 'Tools/SwordClash/generate_content.py'
    if ($LASTEXITCODE -ne 0) { throw 'Scene generation failed.' }
    & dotnet build 'Scripts/RePlayGameScripts.csproj' -c Release --no-restore
    if ($LASTEXITCODE -ne 0) { throw 'C# build failed.' }
}

$output = Join-Path $root 'Saved/SwordClash'
New-Item -ItemType Directory -Force -Path $output | Out-Null

$engine = Join-Path $root 'x64/Release/3dgp.exe'
if (-not (Test-Path -LiteralPath $engine)) { throw 'Build the engine in Release x64 first.' }
Copy-Item -LiteralPath $engine -Destination (Join-Path $output 'SwordClash.exe') -Force
Get-ChildItem -LiteralPath (Split-Path $engine) -Filter '*.dll' | Copy-Item -Destination $output -Force

foreach ($relative in @('resources/Game/SwordClash', 'Shader', 'ThirdParty/DXC/bin/x64',
                        'Managed/RePlayEngine.Managed/bin/Release/net8.0',
                        'Scripts/bin/Release/net8.0')) {
    $source = Join-Path $root $relative
    $destination = Join-Path $output $relative
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Get-ChildItem -LiteralPath $source | Copy-Item -Destination $destination -Recurse -Force
}

# Scene は .replaygame からパスで直接指定する。AssetDatabase への登録は要らない。
New-Item -ItemType Directory -Force -Path (Join-Path $output 'resources') | Out-Null
Write-Utf8 (Join-Path $output 'resources/AssetDatabase.replaydb') "REPLAY_ASSET_DB 2 0`n"

$project = @'
REPLAY_PROJECT 10
STARTUP_SCENE ""
LOADING_SCENE ""
SCENE_FLOW ""
DEFAULT_LANGUAGE "ja"
RENDER_SSAO 1
RENDER_SSR 0
RENDER_TAA 1
RENDER_LUMINANCE_ENABLED 1
RENDER_FINAL_PASS_ENABLED 1
UI_FOCUS_OUTLINE_ENABLED 1
'@
Write-Utf8 (Join-Path $output 'resources/Project.replayproject') $project

$game = @'
REPLAY_GAME 1
NAME SWORD CLASH
STARTUP_SCENE resources/Game/SwordClash/SwordClash.replayscene
WINDOW 1600 900
FULLSCREEN 0
'@
Write-Utf8 (Join-Path $output 'SwordClash.replaygame') $game

$arguments = @('--game')
if ($Frames -gt 0) { $arguments = @('--smoke-test', "$Frames", '--game') }

if ($Headless) {
    $process = Start-Process -FilePath (Join-Path $output 'SwordClash.exe') -WorkingDirectory $output `
        -ArgumentList $arguments -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $output 'stdout.log') `
        -RedirectStandardError (Join-Path $output 'stderr.log')
} else {
    $process = Start-Process -FilePath (Join-Path $output 'SwordClash.exe') -WorkingDirectory $output `
        -ArgumentList $arguments -PassThru
}
Write-Output "SWORD CLASH PID: $($process.Id)"

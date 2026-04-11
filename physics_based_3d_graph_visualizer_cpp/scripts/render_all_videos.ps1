param(
    [string]$OutputDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "videos"),
    [int]$Fps = 60
)

$ErrorActionPreference = "Stop"

$workspace = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $workspace "build\graph_bfs_visualizer.exe"
if (!(Test-Path $exe)) {
    & (Join-Path $PSScriptRoot "build.ps1")
}

New-Item -ItemType Directory -Force $OutputDir | Out-Null

for ($i = 1; $i -le 16; $i++) {
    $video = Join-Path $OutputDir ("Figure{0}.mp4" -f $i)
    Write-Host "Rendering preset $i -> $video"
    & $exe `
        --preset $i `
        --fps $Fps `
        --video-output $video `
        --video-label ("Figure_{0}" -f $i) `
        --hidden `
        --no-interactive

    if ($LASTEXITCODE -ne 0) {
        throw "Rendering preset $i failed."
    }
}

Write-Host "All videos rendered to $OutputDir"

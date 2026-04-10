param(
    [string]$Source = "cpp\\src\\graph_bfs_visualizer.cpp",
    [string]$Output = "build\\graph_bfs_visualizer.exe"
)

$ErrorActionPreference = "Stop"

$workspace = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$cppRoot = Join-Path $workspace "cpp"
$outputPath = Join-Path $workspace $Output
$outputDir = Split-Path -Parent $outputPath

New-Item -ItemType Directory -Force $outputDir | Out-Null

$pkgCFlags = (& pkg-config --cflags glfw3).Trim()
$pkgLibs = (& pkg-config --libs glfw3).Trim()

$args = @(
    "-std=c++23"
    "-O2"
    "-I$($cppRoot)\include"
    (Join-Path $workspace $Source)
    "$($cppRoot)\src\glad.c"
    "-o"
    "$outputPath"
)

if ($pkgCFlags) {
    $args += $pkgCFlags -split "\s+"
}

$args += @("-lopengl32")

if ($pkgLibs) {
    $args += $pkgLibs -split "\s+"
}

Write-Host "Building graph_bfs_visualizer with g++..."
& g++ $args

if ($LASTEXITCODE -ne 0) {
    throw "g++ build failed."
}

Write-Host "Built: $outputPath"

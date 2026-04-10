param(
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

$sources = @(
    (Join-Path $workspace "cpp\src\PlayGround.cpp")
    (Join-Path $workspace "cpp\src\GraphHelper.cpp")
    (Join-Path $workspace "cpp\src\GraphPhysics.cpp")
    (Join-Path $workspace "cpp\src\GraphRender.cpp")
    (Join-Path $workspace "cpp\src\Klotski.cpp")
    (Join-Path $workspace "cpp\src\VideoRecorder.cpp")
    (Join-Path $workspace "cpp\src\glad.c")
)

$args = @(
    "-std=c++23"
    "-O2"
    "-I$($cppRoot)\include"
    $sources
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

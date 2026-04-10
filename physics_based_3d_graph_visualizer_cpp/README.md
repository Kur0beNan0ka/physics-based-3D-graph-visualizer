# Physics-Based 3D Graph Visualizer (C++)

This standalone C++ project refactors the original Python BFS graph-growth visualizer into modules that mirror the source repository more closely:

- `PlayGround.py` -> `cpp/src/PlayGround.cpp`
- `GraphPhysics.py` -> `cpp/src/GraphPhysics.cpp`
- `GraphRender.py` -> `cpp/src/GraphRender.cpp`
- `GraphHelper.py` -> `cpp/src/GraphHelper.cpp`
- `Klotski.py` -> `cpp/src/Klotski.cpp`
- `VideoRecorder.py` -> `cpp/src/VideoRecorder.cpp`

The C++ version keeps the Klotski state-space BFS growth animation, uses Barnes-Hut repulsion plus CPU multithreading for the layout step, and renders the graph with OpenGL.

## Layout

- `cpp/include/GraphCommon.hpp`: shared vector and matrix math
- `cpp/include/GraphHelper.hpp`: edge and adjacency helpers
- `cpp/include/Klotski.hpp`: puzzle presets, state-space graph builder, and puzzle illustration export
- `cpp/include/GraphPhysics.hpp`: BFS growth simulation and Barnes-Hut physics
- `cpp/include/GraphRender.hpp`: GPU renderer and interactive/video entry points
- `cpp/include/VideoRecorder.hpp`: offline frame-sequence recorder
- `cpp/src/PlayGround.cpp`: application entry point
- `cpp/src/glad.c`: GLAD loader source
- `scripts/build.ps1`: build entry
- `scripts/run.cmd`: run entry

The older monolithic file `cpp/src/graph_bfs_visualizer.cpp` is kept as a reference snapshot, but the build now uses the modular sources above.

## Rendering Quality Improvements

Compared with the initial C++ port, the renderer now:

- enables `8x MSAA`
- uses circular point sprites instead of square points
- applies alpha blending to nodes and edges
- frames the graph more tightly with a closer automatic camera
- scales the scene up by default so large graphs fill the window better
- uploads point buffers with reusable GPU capacity to reduce per-frame buffer churn

Rendering is still GPU-backed through OpenGL, while BFS expansion and physics remain CPU-side.

## Build

```powershell
.\scripts\build.ps1
```

or

```cmd
scripts\build.cmd
```

## Run

```cmd
scripts\run.cmd
```

Recommended interactive command:

```cmd
build\graph_bfs_visualizer.exe --preset 16 --threads 4 --physics-iters 1 --nodes-per-frame 20 --theta 1.1 --scene-scale 2.2 --point-size 4.8
```

## Useful Options

```cmd
build\graph_bfs_visualizer.exe --preset 16 --threads 8 --physics-iters 1 --nodes-per-frame 30
build\graph_bfs_visualizer.exe --preset 16 --figure-output figure16.svg --hidden --no-interactive
build\graph_bfs_visualizer.exe --preset 3 --video-output graph3.mp4 --hidden --no-interactive
```

Supported flags:

- `--preset <1..16>`
- `--width <int>`
- `--height <int>`
- `--threads <int>`
- `--physics-iters <int>`
- `--nodes-per-frame <int>`
- `--fps <int>`
- `--theta <float>`
- `--damping <float>`
- `--intensity <float>`
- `--point-size <float>`
- `--line-width <float>`
- `--scene-scale <float>`
- `--viewing-duration <float>`
- `--video-output <path>`
- `--figure-output <path>`
- `--hidden`
- `--no-interactive`

## Video Output

To keep the project dependency-free, `VideoRecorder.cpp` currently writes a `PPM` image sequence into a `*_frames` directory instead of encoding video directly. Each capture folder includes a `README.txt` with an `ffmpeg` command you can run later to turn the frames into `mp4`.

## Notes

- Puzzle presets correspond to the original Python `start_toy1..16`.
- `Klotski.cpp` can now export the puzzle layout as an SVG illustration, replacing the original Matplotlib-based figure export.
- For very large graphs, the main bottleneck is still physics on the CPU rather than raw OpenGL drawing.

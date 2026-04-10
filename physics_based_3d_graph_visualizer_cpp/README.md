# Physics-Based 3D Graph Visualizer (C++)

This is a standalone C++ refactor of the Python BFS graph-growth visualizer. It keeps the Klotski state-space BFS growth flow and replaces the original all-pairs repulsion with a Barnes-Hut 3D approximation for better scaling.

## Layout

- `cpp/src/graph_bfs_visualizer.cpp`: BFS state graph builder, 3D physics simulation, and OpenGL renderer
- `cpp/src/glad.c`: GLAD loader source
- `cpp/include/glad/glad.h`: GLAD header
- `cpp/include/KHR/khrplatform.h`: KHR support header
- `scripts/build.ps1`: build entry
- `scripts/run.cmd`: run entry

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

Useful options:

```cmd
build\graph_bfs_visualizer.exe --preset 16 --threads 4 --physics-iters 1 --nodes-per-frame 20 --theta 1.1
```

## Notes

- Rendering is done on the GPU through OpenGL.
- BFS graph construction and physics simulation currently run on the CPU.
- The source includes presets corresponding to the Python playground puzzles `1..16`.

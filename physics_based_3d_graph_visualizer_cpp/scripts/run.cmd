@echo off
if not exist "%~dp0..\build\graph_bfs_visualizer.exe" call "%~dp0build.cmd"
"%~dp0..\build\graph_bfs_visualizer.exe" %*

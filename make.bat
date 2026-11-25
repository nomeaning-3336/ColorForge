@echo off
rem Builds the ColorForge VST3 and the standalone player (Release, statically linked).
rem Needs on PATH: git, cmake 3.22+, ninja, MinGW-w64 gcc/g++.
setlocal
cd /d "%~dp0"

if not exist "JUCE7\CMakeLists.txt" (
    git clone --depth 1 --branch 7.0.12 https://github.com/juce-framework/JUCE.git JUCE7
)
if not exist "JUCE7\CMakeLists.txt" exit /b 1

rem -mbig-obj: JUCE unity module files exceed the COFF section limit. Env flags also reach the nested juceaide build.
set CXXFLAGS=-Wa,-mbig-obj -fpermissive
set CFLAGS=-Wa,-mbig-obj

cmake -S . -B build-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release -DCF_JUCE_DIR=JUCE7 -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
if errorlevel 1 exit /b 1

cmake --build build-mingw --target ColorForge_VST3 ColorForgePlayer
exit /b %errorlevel%

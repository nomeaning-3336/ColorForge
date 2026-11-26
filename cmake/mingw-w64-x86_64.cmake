# Cross-compile ColorForge for Windows from Linux with the mingw-w64 toolchain (posix threads).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Same flags as make.bat. Set here (not in the environment) so they don't reach the native juceaide build.
set(CMAKE_CXX_FLAGS_INIT "-Wa,-mbig-obj -fpermissive")
set(CMAKE_C_FLAGS_INIT "-Wa,-mbig-obj")

# Cross-compile The Sauce Suite for 64-bit Windows from Linux/macOS with llvm-mingw.
set (CMAKE_SYSTEM_NAME Windows)
set (CMAKE_SYSTEM_PROCESSOR x86_64)
set (CMAKE_C_COMPILER   x86_64-w64-mingw32-clang)
set (CMAKE_CXX_COMPILER x86_64-w64-mingw32-clang++)
set (CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)
set (CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set (CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set (CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# Self-contained DLL: no libc++/unwind DLLs needed on the user's machine
set (CMAKE_SHARED_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
set (CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
set (CMAKE_MODULE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")

@echo off
setlocal
echo ============================================
echo  GlowOrb Desktop - Visual Studio 2022 build
echo ============================================
cmake -S "%~dp0." -B "%~dp0build" -G "Visual Studio 17 2022" -A x64 %*
if errorlevel 1 (
  echo CMake configure FAILED. Make sure CMake 3.20+ and VS2022 (Desktop C++ workload) are installed.
  exit /b 1
)
cmake --build "%~dp0build" --config Release
if errorlevel 1 (
  echo Build FAILED.
  exit /b 1
)
echo.
echo Done: %~dp0build\Release\GlowOrb.exe
echo shaders\ and assets\ were copied next to the .exe automatically.
echo Run it from that folder, or copy the whole Release folder anywhere.
endlocal

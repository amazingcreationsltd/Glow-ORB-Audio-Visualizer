@echo off
rem Minimal build - no CMake needed. Run from a "x64 Native Tools Command Prompt for VS".
setlocal
if not exist out mkdir out
cl /nologo /std:c++17 /EHsc /O2 /W3 /MP ^
   /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS ^
   /I src ^
   src\main.cpp src\App.cpp src\Util.cpp src\Settings.cpp src\Presets.cpp ^
   src\Palette.cpp src\FFT.cpp src\AudioCapture.cpp src\AudioAnalyzer.cpp ^
   src\ParticleSystem.cpp src\D3D12Renderer.cpp src\OverlayWindow.cpp src\SettingsWindow.cpp ^
   /Fe:out\GlowOrb.exe ^
   /link /SUBSYSTEM:WINDOWS ^
   d3d12.lib dxgi.lib dxguid.lib uuid.lib ole32.lib propsys.lib ^
   user32.lib gdi32.lib shell32.lib comctl32.lib advapi32.lib winmm.lib
if errorlevel 1 exit /b 1
xcopy /E /I /Y shaders out\shaders >nul
xcopy /E /I /Y assets out\assets >nul
echo.
echo Done: out\GlowOrb.exe
endlocal

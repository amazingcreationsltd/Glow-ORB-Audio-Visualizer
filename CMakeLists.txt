cmake_minimum_required(VERSION 3.20)
project(GlowOrbDesktop VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "GlowOrb Desktop only builds for Windows (DirectX 12 / WASAPI).")
endif()

set(GLOW_SOURCES
    src/main.cpp
    src/App.cpp
    src/Util.cpp
    src/Settings.cpp
    src/Presets.cpp
    src/Palette.cpp
    src/FFT.cpp
    src/AudioCapture.cpp
    src/AudioAnalyzer.cpp
    src/ParticleSystem.cpp
    src/D3D12Renderer.cpp
    src/OverlayWindow.cpp
    src/SettingsWindow.cpp
)

add_executable(GlowOrb WIN32 ${GLOW_SOURCES})

target_include_directories(GlowOrb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(GlowOrb PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00
)

if(MINGW)
    # -municode makes the CRT look for wWinMain instead of WinMain.
    # Static libgcc/libstdc++ keeps a cross-built .exe self-contained.
    target_compile_options(GlowOrb PRIVATE -municode)
    target_link_options(GlowOrb PRIVATE -municode -static-libgcc -static-libstdc++)
    target_link_libraries(GlowOrb PRIVATE
        d3d12 dxgi dxguid uuid ole32 propsys
        user32 gdi32 shell32 comctl32 advapi32 winmm
    )
else()
    target_link_libraries(GlowOrb PRIVATE
        d3d12.lib dxgi.lib dxguid.lib uuid.lib ole32.lib propsys.lib
        user32.lib gdi32.lib shell32.lib comctl32.lib advapi32.lib winmm.lib
    )
    target_compile_options(GlowOrb PRIVATE /W3 /permissive- /EHsc)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT GlowOrb)
endif()

# HLSL is compiled at runtime with D3DCompileFromFile (d3dcompiler_47.dll),
# so the .hlsl file and the icon must sit next to the executable.
add_custom_command(TARGET GlowOrb POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:GlowOrb>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:GlowOrb>/assets"
    COMMENT "Copying shaders and assets next to GlowOrb.exe"
)

# Also place a copy in build/Release for the direct-cl.bat workflow.
install(TARGETS GlowOrb RUNTIME DESTINATION .)
install(DIRECTORY shaders/ DESTINATION shaders)
install(DIRECTORY assets/ DESTINATION assets)

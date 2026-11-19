cmake_minimum_required(VERSION 3.22.1)
project(smash64_android_recomp LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

option(ENABLE_VK_VALIDATION "Enable Vulkan validation layers in debug builds" OFF)

# ------------------------------------------------------------------------------
# 1. SDL3 (Window, Lifecycle, Gamepad, Multi-Touch, Vulkan Surface)
# ------------------------------------------------------------------------------
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)

set(LOCAL_SDL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../third_party/SDL")
if(EXISTS "${LOCAL_SDL_DIR}/CMakeLists.txt")
    add_subdirectory("${LOCAL_SDL_DIR}" "${CMAKE_BINARY_DIR}/sdl3-build")
else()
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.8
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SDL3)
endif()

# ------------------------------------------------------------------------------
# 2. Google Oboe (Low-Latency AAudio / OpenSL ES)
# ------------------------------------------------------------------------------
FetchContent_Declare(
    oboe
    GIT_REPOSITORY https://github.com/google/oboe.git
    GIT_TAG 1.9.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(oboe)

# ------------------------------------------------------------------------------
# 3. GLM (Vector / Matrix Math)
# ------------------------------------------------------------------------------
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)

# ------------------------------------------------------------------------------
# 4. Dear ImGui (Touch Controls Overlay & Performance HUD over Vulkan)
# ------------------------------------------------------------------------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_sdl_vulkan STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui_sdl_vulkan PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_sdl_vulkan PUBLIC
    SDL3::SDL3
    vulkan
)
target_compile_definitions(imgui_sdl_vulkan PUBLIC
    VK_USE_PLATFORM_ANDROID_KHR=1
)

# ------------------------------------------------------------------------------
# 5. libchdr (Streaming CHD Decompression for Launcher)
# ------------------------------------------------------------------------------
set(WITH_SYSTEM_ZLIB ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(
    libchdr
    GIT_REPOSITORY https://github.com/rtissera/libchdr.git
    GIT_TAG 8968155a1410b9408b55d86e92055fa220987633
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(libchdr)
set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED})

# ------------------------------------------------------------------------------
# 6. Launcher JNI Library (liblauncher_jni.so)
#    Handles SAF File Descriptor streaming, magic-byte detection,
#    .z64/.n64/.v64 byte-swapping, .zip/.chd/.iso extraction, and SHA-256.
# ------------------------------------------------------------------------------
add_library(launcher_jni SHARED
    launcher_jni/launcher_jni.cpp
    launcher_jni/sha256.cpp
)
target_include_directories(launcher_jni PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/launcher_jni
)
target_link_libraries(launcher_jni PRIVATE
    chdr-static
    z
    android
    log
)
target_compile_options(launcher_jni PRIVATE
    -Wall -Wextra -Werror=return-type
)

# ------------------------------------------------------------------------------
# 7. Main Recompiled Game Library (libgame_recomp.so)
# ------------------------------------------------------------------------------
add_library(game_recomp SHARED
    recomp_generated/recomp_entrypoint.cpp
    recomp_generated/recomp_lookup.cpp
    platform_shim/n64_memory.cpp
    platform_shim/n64_os_shim.cpp
    graphics/vulkan_renderer.cpp
    audio/oboe_audio.cpp
    input/input_manager.cpp
)

target_include_directories(game_recomp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/recomp_generated
    ${CMAKE_CURRENT_SOURCE_DIR}/platform_shim
    ${CMAKE_CURRENT_SOURCE_DIR}/graphics
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/input
)

target_link_libraries(game_recomp PRIVATE
    SDL3::SDL3
    oboe::oboe
    imgui_sdl_vulkan
    glm::glm
    vulkan
    dl
    android
    log
)

target_compile_definitions(game_recomp PRIVATE
    VK_USE_PLATFORM_ANDROID_KHR=1
    $<$<BOOL:${ENABLE_VK_VALIDATION}>:SMASH64_ENABLE_VK_VALIDATION=1>
)

target_compile_options(game_recomp PRIVATE
    -Wall -Wextra -Werror=return-type
)

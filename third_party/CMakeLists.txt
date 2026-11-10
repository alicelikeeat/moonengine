# Vendored third-party code. Every entry is recorded with its version,
# upstream commit or archive hash, and licence in dependencies.json.
# Third-party targets do not get Aether's warning flags.

# Vulkan C headers (Apache-2.0 OR MIT).
add_library(aether_vulkan_headers INTERFACE)
target_include_directories(aether_vulkan_headers SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/vulkan-headers/include)

# volk: loads Vulkan entry points at run time, so the engine needs no
# link-time Vulkan SDK and starts (with a clear error) even without a driver.
add_library(aether_volk STATIC volk/volk.c)
target_include_directories(aether_volk SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/volk)
target_link_libraries(aether_volk PUBLIC aether_vulkan_headers ${CMAKE_DL_LIBS})
target_compile_definitions(aether_volk PUBLIC VK_NO_PROTOTYPES)
set_target_properties(aether_volk PROPERTIES POSITION_INDEPENDENT_CODE ON)

# GLFW: windows and input, used by samples only (the engine never depends on it).
if(AETHER_BUILD_SAMPLES)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    add_subdirectory(glfw EXCLUDE_FROM_ALL)
endif()

# Dear ImGui (MIT), docking branch: immediate-mode UI for the editor and
# debug overlays. Core plus the Vulkan renderer backend (used by
# engine/render) and the GLFW platform backend (applications only).
add_library(aether_imgui STATIC
    imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp imgui/imgui_demo.cpp)
target_include_directories(aether_imgui SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
target_compile_definitions(aether_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

add_library(aether_imgui_vulkan STATIC imgui/backends/imgui_impl_vulkan.cpp)
target_include_directories(aether_imgui_vulkan SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends)
target_compile_definitions(aether_imgui_vulkan PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
target_link_libraries(aether_imgui_vulkan PUBLIC aether_imgui aether_volk)

# ImGuizmo (MIT): translate/rotate/scale manipulators drawn with ImGui.
add_library(aether_imguizmo STATIC imguizmo/ImGuizmo.cpp)
target_include_directories(aether_imguizmo SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/imguizmo)
target_link_libraries(aether_imguizmo PUBLIC aether_imgui)

if(AETHER_BUILD_SAMPLES)
    add_library(aether_imgui_glfw STATIC imgui/backends/imgui_impl_glfw.cpp)
    target_include_directories(aether_imgui_glfw SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends)
    target_link_libraries(aether_imgui_glfw PUBLIC aether_imgui glfw)
    target_compile_definitions(aether_imgui_glfw PRIVATE GLFW_INCLUDE_NONE)
endif()

# stb_image (MIT or public domain): PNG/JPEG decoding for imported assets.
add_library(aether_stb_image STATIC stb/stb_image_impl.c)
target_include_directories(aether_stb_image SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)
target_compile_definitions(aether_stb_image PUBLIC STBI_ONLY_PNG STBI_ONLY_JPEG STBI_NO_STDIO STBI_NO_LINEAR STBI_MAX_DIMENSIONS=16384)

# Jolt Physics (MIT): rigid body simulation behind engine/simulation.
# Built with a fixed, conservative configuration; the definitions are PUBLIC
# because Jolt's headers must see the same ones as its sources.
file(GLOB_RECURSE _jolt_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/jolt/Jolt/*.cpp)
add_library(aether_jolt STATIC ${_jolt_sources})
target_include_directories(aether_jolt SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/jolt)
target_compile_features(aether_jolt PUBLIC cxx_std_17)
# No asserts, profiler, debug renderer or object stream: the engine checks
# its own inputs and draws its own debug shapes.
target_compile_definitions(aether_jolt PUBLIC JPH_DISABLE_CUSTOM_ALLOCATOR)
if(MSVC)
    target_compile_options(aether_jolt PRIVATE /W0 /EHsc)
else()
    # SSE4.2 + POPCNT: every x86-64 CPU since about 2009. Wider instruction
    # sets are left off so one binary runs everywhere.
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-msse4.2 AETHER_HAS_SSE42)
    if(AETHER_HAS_SSE42 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        target_compile_options(aether_jolt PUBLIC -msse4.2 -mpopcnt)
    endif()
    target_compile_options(aether_jolt PRIVATE -w)
endif()
set_target_properties(aether_jolt PROPERTIES POSITION_INDEPENDENT_CODE ON UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 32)
find_package(Threads REQUIRED)
target_link_libraries(aether_jolt PUBLIC Threads::Threads)

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

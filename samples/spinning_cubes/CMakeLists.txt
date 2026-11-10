add_executable(aether_spinning_cubes main.cpp)
target_link_libraries(aether_spinning_cubes PRIVATE
    Aether::project_options Aether::render Aether::simulation Aether::gameplay Aether::world Aether::foundation glfw aether_vulkan_headers)
# GLFW only needs the Vulkan type declarations to expose glfwCreateWindowSurface.
target_compile_definitions(aether_spinning_cubes PRIVATE VK_NO_PROTOTYPES GLFW_INCLUDE_NONE)
if(WIN32)
    # A normal windowed app on Windows, but keep main() as the entry point.
    set_target_properties(aether_spinning_cubes PROPERTIES WIN32_EXECUTABLE ON)
    if(MSVC)
        target_link_options(aether_spinning_cubes PRIVATE /ENTRY:mainCRTStartup)
    endif()
endif()

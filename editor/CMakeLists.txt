aether_add_module(editor
    SOURCES
        src/demo_scene.cpp
        src/editor_app.cpp
        src/editor_camera.cpp
        src/picking.cpp
        src/snapshot.cpp
        src/theme.cpp
        src/undo.cpp
    DEPENDS
        foundation
        world
        render)
target_link_libraries(aether_editor PRIVATE aether_imgui aether_imguizmo)

if(AETHER_BUILD_TESTS)
    aether_add_test(aether_editor_tests
        SOURCES
            tests/test_camera_picking.cpp
            tests/test_snapshot_undo.cpp
        LINK
            Aether::editor)
endif()

if(AETHER_BUILD_SAMPLES)
    add_executable(aether_editor_app app/main.cpp)
    set_target_properties(aether_editor_app PROPERTIES OUTPUT_NAME aether_editor)
    target_link_libraries(aether_editor_app PRIVATE
        Aether::project_options Aether::editor aether_imgui_glfw aether_imguizmo aether_vulkan_headers)
    target_compile_definitions(aether_editor_app PRIVATE VK_NO_PROTOTYPES GLFW_INCLUDE_NONE)
    if(WIN32)
        set_target_properties(aether_editor_app PROPERTIES WIN32_EXECUTABLE ON)
        if(MSVC)
            target_link_options(aether_editor_app PRIVATE /ENTRY:mainCRTStartup)
        endif()
    endif()
endif()

aether_add_module(render
    SOURCES
        src/camera.cpp
        src/mesh.cpp
        src/mesh_registry.cpp
        src/model_library.cpp
        src/vulkan_renderer.cpp
    DEPENDS
        foundation
        world
        assets)
# Vulkan stays private: the public headers expose no Vulkan types.
target_link_libraries(aether_render PRIVATE aether_volk aether_imgui_vulkan)
# `VkFooCreateInfo info{VK_STRUCTURE_TYPE_...}` value-initialises the other
# members on purpose; GCC/Clang warn about it under -Wextra.
if(NOT MSVC)
    set_source_files_properties(src/vulkan_renderer.cpp PROPERTIES
        COMPILE_OPTIONS "-Wno-missing-field-initializers")
endif()

if(AETHER_BUILD_TESTS)
    aether_add_test(aether_render_tests
        SOURCES
            tests/test_camera_mesh.cpp
            tests/test_mesh_registry.cpp
            tests/test_renderer_headless.cpp
        LINK
            Aether::render)
    target_link_libraries(aether_render_tests PRIVATE ${CMAKE_DL_LIBS})
    target_compile_definitions(aether_render_tests PRIVATE
        AETHER_SAMPLE_CONTENT="${PROJECT_SOURCE_DIR}/samples/content")
    # GPU tests run on the Mesa software driver in CI; suppress sanitizer
    # reports that originate inside the (uninstrumented) driver only.
    set(_san_dir ${PROJECT_SOURCE_DIR}/tools/sanitizers)
    set_tests_properties(aether_render_tests PROPERTIES ENVIRONMENT
        "LSAN_OPTIONS=suppressions=${_san_dir}/render-lsan.supp;TSAN_OPTIONS=suppressions=${_san_dir}/render-tsan.supp")
endif()

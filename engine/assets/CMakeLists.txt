aether_add_module(assets
    SOURCES
        src/gltf.cpp
        src/image.cpp
    DEPENDS
        foundation)
target_link_libraries(aether_assets PRIVATE aether_stb_image)

if(AETHER_BUILD_TESTS)
    aether_add_test(aether_assets_tests
        SOURCES
            tests/test_gltf.cpp
        LINK
            Aether::assets)
endif()

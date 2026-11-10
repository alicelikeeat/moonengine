aether_add_module(simulation
    SOURCES
        src/physics.cpp
        src/serialization.cpp
    DEPENDS
        foundation
        world)
# Jolt stays private: the public headers expose no Jolt types.
target_link_libraries(aether_simulation PRIVATE aether_jolt)
if(NOT MSVC)
    # Jolt's headers are included as system headers, but its macros expand
    # in our code.
    set_source_files_properties(src/physics.cpp PROPERTIES COMPILE_OPTIONS "-Wno-old-style-cast")
endif()

if(AETHER_BUILD_TESTS)
    aether_add_test(aether_simulation_tests
        SOURCES
            tests/test_physics.cpp
        LINK
            Aether::simulation)
endif()

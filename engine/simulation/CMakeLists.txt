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
    set(_jolt_user_flags -Wno-old-style-cast)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # Jolt's reference counting uses std::atomic_thread_fence, which GCC
        # warns ThreadSanitizer cannot model (-Wtsan). The fence is inside
        # Jolt; our code does not rely on it.
        list(APPEND _jolt_user_flags -Wno-tsan)
    endif()
    set_source_files_properties(src/physics.cpp PROPERTIES COMPILE_OPTIONS "${_jolt_user_flags}")
endif()

if(AETHER_BUILD_TESTS)
    aether_add_test(aether_simulation_tests
        SOURCES
            tests/test_physics.cpp
        LINK
            Aether::simulation)
endif()

aether_add_module(gameplay
    SOURCES
        src/spin.cpp
    DEPENDS
        foundation
        world)

if(AETHER_BUILD_TESTS)
    aether_add_test(aether_gameplay_tests
        SOURCES
            tests/test_spin.cpp
        LINK
            Aether::gameplay)
endif()

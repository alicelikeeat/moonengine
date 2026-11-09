add_library(aether_test_main STATIC aether_test.cpp)
add_library(Aether::test_main ALIAS aether_test_main)
target_include_directories(aether_test_main PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aether_test_main PRIVATE Aether::project_options)

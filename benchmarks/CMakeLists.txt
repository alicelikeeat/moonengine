add_executable(aether_foundation_bench foundation_bench.cpp)
target_link_libraries(aether_foundation_bench PRIVATE Aether::project_options Aether::foundation)

add_executable(aether_world_bench world_bench.cpp)
target_link_libraries(aether_world_bench PRIVATE Aether::project_options Aether::world)

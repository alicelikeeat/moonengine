# aether_add_module(<name>
#     SOURCES <files...>
#     [DEPENDS <aether modules...>]
#     [LINK <other libraries...>])
#
# Declares an engine module as a static library named aether_<name> with
# alias Aether::<name>. Public headers live in <module>/include/aether/<name>.
# Module-to-module dependencies must be declared through DEPENDS so the
# layering in docs/architecture/overview.md stays visible in the build.

set(AETHER_MODULE_LAYER_foundation 0)
set(AETHER_MODULE_LAYER_world 1)
set(AETHER_MODULE_LAYER_assets 1)
set(AETHER_MODULE_LAYER_render 2)
set(AETHER_MODULE_LAYER_simulation 2)
set(AETHER_MODULE_LAYER_animation 2)
set(AETHER_MODULE_LAYER_gameplay 3)
set(AETHER_MODULE_LAYER_intelligence 4)
set(AETHER_MODULE_LAYER_editor 5)

function(aether_add_module name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPENDS;LINK" ${ARGN})

    if(NOT DEFINED AETHER_MODULE_LAYER_${name})
        message(FATAL_ERROR "Module '${name}' has no layer in AetherModule.cmake")
    endif()
    set(_layer ${AETHER_MODULE_LAYER_${name}})

    foreach(dep IN LISTS ARG_DEPENDS)
        if(NOT DEFINED AETHER_MODULE_LAYER_${dep})
            message(FATAL_ERROR "Module '${name}' depends on unknown module '${dep}'")
        endif()
        if(NOT AETHER_MODULE_LAYER_${dep} LESS _layer)
            message(FATAL_ERROR
                "Layering violation: '${name}' (layer ${_layer}) may not depend on "
                "'${dep}' (layer ${AETHER_MODULE_LAYER_${dep}}). See docs/architecture/overview.md")
        endif()
    endforeach()

    add_library(aether_${name} STATIC ${ARG_SOURCES})
    add_library(Aether::${name} ALIAS aether_${name})
    target_include_directories(aether_${name}
        PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(aether_${name} PRIVATE Aether::project_options)
    foreach(dep IN LISTS ARG_DEPENDS)
        target_link_libraries(aether_${name} PUBLIC Aether::${dep})
    endforeach()
    if(ARG_LINK)
        target_link_libraries(aether_${name} PUBLIC ${ARG_LINK})
    endif()
endfunction()

# aether_add_test(<name> SOURCES <files...> LINK <libs...>)
function(aether_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LINK" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Aether::project_options Aether::test_main ${ARG_LINK})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 120)
endfunction()

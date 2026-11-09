# Shared compiler settings applied to every Aether target through
# the aether_project_options interface library.

add_library(aether_project_options INTERFACE)
add_library(Aether::project_options ALIAS aether_project_options)

target_compile_features(aether_project_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(aether_project_options INTERFACE
        /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc
        /wd4324)  # C4324: padding added by alignas, intended for cache-line isolation
    if(AETHER_WARNINGS_AS_ERRORS)
        target_compile_options(aether_project_options INTERFACE /WX)
    endif()
    target_compile_definitions(aether_project_options INTERFACE
        NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(aether_project_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
        -Wold-style-cast -Wcast-align -Woverloaded-virtual)
    if(AETHER_WARNINGS_AS_ERRORS)
        target_compile_options(aether_project_options INTERFACE -Werror)
    endif()
endif()

if(AETHER_SANITIZER)
    if(MSVC)
        if(AETHER_SANITIZER STREQUAL "address")
            target_compile_options(aether_project_options INTERFACE /fsanitize=address)
        else()
            message(FATAL_ERROR "MSVC only supports AETHER_SANITIZER=address")
        endif()
    else()
        if(AETHER_SANITIZER STREQUAL "address")
            set(_aether_san_flags -fsanitize=address,undefined -fno-omit-frame-pointer)
        elseif(AETHER_SANITIZER STREQUAL "thread")
            set(_aether_san_flags -fsanitize=thread -fno-omit-frame-pointer)
        elseif(AETHER_SANITIZER STREQUAL "undefined")
            set(_aether_san_flags -fsanitize=undefined -fno-sanitize-recover=all)
        else()
            message(FATAL_ERROR "Unknown AETHER_SANITIZER '${AETHER_SANITIZER}'")
        endif()
        target_compile_options(aether_project_options INTERFACE ${_aether_san_flags})
        target_link_options(aether_project_options INTERFACE ${_aether_san_flags})
    endif()
endif()

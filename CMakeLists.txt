cmake_minimum_required(VERSION 3.22)
project(forge_gpu_demo LANGUAGES C CXX)

include(cmake/ForgeGpuSources.cmake)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(FORGE_GPU_SDL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL" CACHE PATH "SDL source tree or empty to use find_package(SDL3)")
set(FORGE_GPU_SHADERCROSS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL_shadercross" CACHE PATH "SDL_shadercross source tree used to locate shadercross tools")
set(FORGE_GPU_SHADERCROSS_EXECUTABLE "" CACHE FILEPATH "shadercross executable used to generate shader headers")
set(FORGE_GPU_TINT_EXECUTABLE "" CACHE FILEPATH "Tint executable used to validate generated WGSL")
set(FORGE_GPU_GENERATE_SHADERS ON CACHE BOOL "Generate Forge shader headers from HLSL during the build")
set(FORGE_GPU_WEB_RELEASE OFF CACHE BOOL "Use the polished browser shell and release-oriented Emscripten link options")
set(FORGE_GPU_WEB_SHELL_FILE "" CACHE FILEPATH "Optional Emscripten shell HTML file for browser builds")
set(FORGE_GPU_SHADERCROSS_BUILD_DIR "${FORGE_GPU_SHADERCROSS_SOURCE_DIR}/build" CACHE PATH "SDL_shadercross build directory used to locate shadercross tools")
set(FORGE_GPU_SHADERCROSS_INSTALL_DIR "${FORGE_GPU_SHADERCROSS_BUILD_DIR}/install" CACHE PATH "SDL_shadercross install prefix used to locate shadercross tools")
set(FORGE_GPU_SHADER_TOOL_RUNTIME_DIRS "" CACHE STRING "Extra runtime library directories needed by host shader tools")

function(forge_gpu_apply_no_fp_contract_options TARGET_NAME)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /fp:precise)
    else()
        target_compile_options(${TARGET_NAME} PRIVATE -ffp-contract=off)
    endif()
endfunction()

function(forge_gpu_apply_path_remap_options TARGET_NAME)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${TARGET_NAME} PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.>"
        )
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${TARGET_NAME} PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.>"
        )
    endif()
endfunction()

function(forge_gpu_find_host_tool OUT_VAR TOOL_NAME)
    foreach(TOOL_DIR ${ARGN})
        set(TOOL_SEARCH_DIRS "${TOOL_DIR}")
        foreach(CONFIG_DIR Debug Release RelWithDebInfo MinSizeRel)
            list(APPEND TOOL_SEARCH_DIRS "${TOOL_DIR}/${CONFIG_DIR}")
        endforeach()
        foreach(TOOL_SEARCH_DIR ${TOOL_SEARCH_DIRS})
            if(CMAKE_HOST_WIN32)
                set(CANDIDATE "${TOOL_SEARCH_DIR}/${TOOL_NAME}.exe")
                if(EXISTS "${CANDIDATE}")
                    set(${OUT_VAR} "${CANDIDATE}" PARENT_SCOPE)
                    return()
                endif()
            endif()
            set(CANDIDATE "${TOOL_SEARCH_DIR}/${TOOL_NAME}")
            if(EXISTS "${CANDIDATE}")
                set(${OUT_VAR} "${CANDIDATE}" PARENT_SCOPE)
                return()
            endif()
        endforeach()
    endforeach()
endfunction()

function(forge_gpu_append_existing_dir LIST_VAR DIR)
    if(DIR AND EXISTS "${DIR}")
        get_filename_component(ABS_DIR "${DIR}" ABSOLUTE)
        list(APPEND ${LIST_VAR} "${ABS_DIR}")
        list(REMOVE_DUPLICATES ${LIST_VAR})
        set(${LIST_VAR} "${${LIST_VAR}}" PARENT_SCOPE)
    endif()
endfunction()

function(forge_gpu_append_prefix_runtime_dirs LIST_VAR)
    foreach(PREFIX ${ARGN})
        forge_gpu_append_existing_dir(${LIST_VAR} "${PREFIX}/bin")
        forge_gpu_append_existing_dir(${LIST_VAR} "${PREFIX}/lib")
    endforeach()
    set(${LIST_VAR} "${${LIST_VAR}}" PARENT_SCOPE)
endfunction()

function(forge_gpu_append_target_runtime_dir LIST_VAR TARGET_NAME)
    if(NOT TARGET "${TARGET_NAME}")
        return()
    endif()

    foreach(PROPERTY_NAME
        IMPORTED_LOCATION
        IMPORTED_LOCATION_RELEASE
        IMPORTED_LOCATION_RELWITHDEBINFO
        IMPORTED_LOCATION_MINSIZEREL
        IMPORTED_LOCATION_DEBUG
    )
        get_target_property(TARGET_LOCATION "${TARGET_NAME}" "${PROPERTY_NAME}")
        if(TARGET_LOCATION AND IS_ABSOLUTE "${TARGET_LOCATION}")
            get_filename_component(TARGET_DIR "${TARGET_LOCATION}" DIRECTORY)
            forge_gpu_append_existing_dir(${LIST_VAR} "${TARGET_DIR}")
        endif()
    endforeach()
    set(${LIST_VAR} "${${LIST_VAR}}" PARENT_SCOPE)
endfunction()

if(NOT FORGE_GPU_SHADERCROSS_EXECUTABLE)
    forge_gpu_find_host_tool(FORGE_GPU_SHADERCROSS_EXECUTABLE shadercross
        "${FORGE_GPU_SHADERCROSS_BUILD_DIR}"
        "${FORGE_GPU_SHADERCROSS_INSTALL_DIR}/bin"
    )
endif()

if(NOT FORGE_GPU_TINT_EXECUTABLE)
    forge_gpu_find_host_tool(FORGE_GPU_TINT_EXECUTABLE shadercross-tint
        "${FORGE_GPU_SHADERCROSS_BUILD_DIR}"
        "${FORGE_GPU_SHADERCROSS_BUILD_DIR}/bundled-tint/bin"
        "${FORGE_GPU_SHADERCROSS_INSTALL_DIR}/libexec/SDL3_shadercross"
    )
endif()

set(FORGE_GPU_HAVE_SDL_TARGET OFF)
if(EMSCRIPTEN)
    find_package(SDL3 CONFIG QUIET COMPONENTS SDL3-static)
    if(TARGET SDL3::SDL3-static OR TARGET SDL3-static)
        set(FORGE_GPU_HAVE_SDL_TARGET ON)
    endif()
else()
    find_package(SDL3 CONFIG QUIET)
    if(TARGET SDL3::SDL3 OR TARGET SDL3::SDL3-static OR TARGET SDL3-static)
        set(FORGE_GPU_HAVE_SDL_TARGET ON)
    endif()
endif()

if(NOT FORGE_GPU_HAVE_SDL_TARGET AND EXISTS "${FORGE_GPU_SDL_SOURCE_DIR}/CMakeLists.txt")
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    add_subdirectory("${FORGE_GPU_SDL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/SDL")
elseif(EMSCRIPTEN)
    find_package(SDL3 CONFIG REQUIRED COMPONENTS SDL3-static)
else()
    find_package(SDL3 CONFIG REQUIRED)
endif()

if(EMSCRIPTEN)
    set(FORGE_GPU_EMDAWN_PREWARM_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/forge_gpu_emdawn_prewarm.c")
    set(FORGE_GPU_EMDAWN_PREWARM_OBJECT "${CMAKE_CURRENT_BINARY_DIR}/forge_gpu_emdawn_prewarm.o")
    set(FORGE_GPU_EMDAWN_PREWARM_STAMP "${CMAKE_CURRENT_BINARY_DIR}/forge_gpu_emdawn_prewarm.stamp")
    file(WRITE "${FORGE_GPU_EMDAWN_PREWARM_SOURCE}" "int forge_gpu_emdawn_prewarm(void) { return 0; }\n")
    set(FORGE_GPU_EMDAWN_PREWARM_COMMAND
        "${CMAKE_C_COMPILER}" "--use-port=emdawnwebgpu:cpp_bindings=false" -c "${FORGE_GPU_EMDAWN_PREWARM_SOURCE}" -o "${FORGE_GPU_EMDAWN_PREWARM_OBJECT}"
    )
    if(CMAKE_HOST_WIN32)
        set(FORGE_GPU_EMDAWN_PREWARM_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/forge_gpu_emdawn_prewarm.cmd")
        file(WRITE "${FORGE_GPU_EMDAWN_PREWARM_SCRIPT}"
            "@echo off\r\n"
            "for %%I in (\"${CMAKE_C_COMPILER}\") do set \"FORGE_GPU_EMCC=%%~sI\"\r\n"
            "call \"%FORGE_GPU_EMCC%\" --use-port=emdawnwebgpu:cpp_bindings=false -c \"${FORGE_GPU_EMDAWN_PREWARM_SOURCE}\" -o \"${FORGE_GPU_EMDAWN_PREWARM_OBJECT}\"\r\n"
        )
        set(FORGE_GPU_EMDAWN_PREWARM_COMMAND "${FORGE_GPU_EMDAWN_PREWARM_SCRIPT}")
    endif()
    add_custom_command(
        OUTPUT "${FORGE_GPU_EMDAWN_PREWARM_STAMP}"
        COMMAND ${FORGE_GPU_EMDAWN_PREWARM_COMMAND}
        COMMAND "${CMAKE_COMMAND}" -E touch "${FORGE_GPU_EMDAWN_PREWARM_STAMP}"
        DEPENDS "${FORGE_GPU_EMDAWN_PREWARM_SOURCE}"
        VERBATIM
    )
    add_custom_target(forge-gpu-prewarm-emdawn DEPENDS "${FORGE_GPU_EMDAWN_PREWARM_STAMP}")
    if(TARGET SDL3-static)
        add_dependencies(SDL3-static forge-gpu-prewarm-emdawn)
    endif()
endif()

set(FORGE_GPU_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(FORGE_GPU_GENERATED_SHADER_DIR "${FORGE_GPU_GENERATED_DIR}/shaders/generated")
set(FORGE_GPU_GENERATED_LAYOUT_HEADER "${FORGE_GPU_GENERATED_DIR}/forge_gpu_shader_layouts.h")
set(FORGE_GPU_GENERATED_LAYOUT_SOURCE "${FORGE_GPU_GENERATED_DIR}/forge_gpu_shader_layouts.c")
set(FORGE_GPU_GENERATED_STAMP "${FORGE_GPU_GENERATED_DIR}/forge_gpu_shaders.stamp")

file(GLOB_RECURSE FORGE_GPU_SHADER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/*.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/*.hlsli"
)

if(FORGE_GPU_GENERATE_SHADERS)
    if(NOT FORGE_GPU_SHADERCROSS_EXECUTABLE OR NOT EXISTS "${FORGE_GPU_SHADERCROSS_EXECUTABLE}")
        message(FATAL_ERROR "FORGE_GPU_SHADERCROSS_EXECUTABLE is required to generate shader headers.")
    endif()
    if(NOT FORGE_GPU_TINT_EXECUTABLE OR NOT EXISTS "${FORGE_GPU_TINT_EXECUTABLE}")
        message(FATAL_ERROR "FORGE_GPU_TINT_EXECUTABLE is required to generate shader headers.")
    endif()

    get_filename_component(FORGE_GPU_SHADERCROSS_EXECUTABLE_DIR "${FORGE_GPU_SHADERCROSS_EXECUTABLE}" DIRECTORY)
    get_filename_component(FORGE_GPU_TINT_EXECUTABLE_DIR "${FORGE_GPU_TINT_EXECUTABLE}" DIRECTORY)
    set(FORGE_GPU_SHADER_TOOL_LIBRARY_PATH_DIRS)
    forge_gpu_append_existing_dir(FORGE_GPU_SHADER_TOOL_LIBRARY_PATH_DIRS "${FORGE_GPU_SHADERCROSS_EXECUTABLE_DIR}")
    forge_gpu_append_existing_dir(FORGE_GPU_SHADER_TOOL_LIBRARY_PATH_DIRS "${FORGE_GPU_TINT_EXECUTABLE_DIR}")
    forge_gpu_append_existing_dir(FORGE_GPU_SHADER_TOOL_LIBRARY_PATH_DIRS "${FORGE_GPU_SHADERCROSS_BUILD_DIR}")
    forge_gpu_append_existing_dir(FORGE_GPU_SHADER_TOOL_LIBRARY_PATH_DIRS "${FORGE_GPU_SHADERCROSS_BUILD_DIR}/external/SPIRV-Cross")
    forge_gpu_append_existing_dir(FORGE_GPU_SHADER_TOOL_LIBRARY_PATH_DIRS "${FORGE_GPU_SHADERCROSS_BUILD_DIR}/external/DirectXShaderCompiler/lib")
    forge_gpu_append_existing_dir(FORGE_GPU_SHADER_TOOL_LIBRARY_PATH_DIRS "${FORGE_GPU_SHADERCROSS_INSTALL_DIR}/bin")
    forge_gpu_append_existing_dir(FORGE_GPU_SHADER_TOOL_LIBRARY_PATH_DIRS "${FORGE_GPU_SHADERCROSS_INSTALL_DIR}/lib")
    forge_gpu_append_prefix_runtime_dirs(FORGE_GPU_SHADER_TOOL_LIBRARY_PATH_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/build/deps")
    forge_gpu_append_prefix_runtime_dirs(FORGE_GPU_SHADER_TOOL_LIBRARY_PATH_DIRS "${CMAKE_CURRENT_BINARY_DIR}/../deps")
    forge_gpu_append_prefix_runtime_dirs(FORGE_GPU_SHADER_TOOL_LIBRARY_PATH_DIRS ${CMAKE_PREFIX_PATH})
    foreach(RUNTIME_DIR ${FORGE_GPU_SHADER_TOOL_RUNTIME_DIRS})
        forge_gpu_append_existing_dir(FORGE_GPU_SHADER_TOOL_LIBRARY_PATH_DIRS "${RUNTIME_DIR}")
    endforeach()
    forge_gpu_append_target_runtime_dir(FORGE_GPU_SHADER_TOOL_LIBRARY_PATH_DIRS SDL3::SDL3)
    forge_gpu_append_target_runtime_dir(FORGE_GPU_SHADER_TOOL_LIBRARY_PATH_DIRS SDL3::SDL3-static)
    forge_gpu_append_target_runtime_dir(FORGE_GPU_SHADER_TOOL_LIBRARY_PATH_DIRS SDL3-static)
    forge_gpu_append_existing_dir(FORGE_GPU_SHADER_TOOL_LIBRARY_PATH_DIRS "${FORGE_GPU_SHADERCROSS_SOURCE_DIR}/external/DirectXShaderCompiler-binaries/linux/lib")
    foreach(DXC_WINDOWS_ARCH x64 arm64 x86)
        forge_gpu_append_existing_dir(FORGE_GPU_SHADER_TOOL_LIBRARY_PATH_DIRS "${FORGE_GPU_SHADERCROSS_SOURCE_DIR}/external/DirectXShaderCompiler-binaries/windows/bin/${DXC_WINDOWS_ARCH}")
    endforeach()

    set(FORGE_GPU_SHADER_TOOL_ENV)
    if(FORGE_GPU_SHADER_TOOL_LIBRARY_PATH_DIRS)
        cmake_path(CONVERT "${FORGE_GPU_SHADER_TOOL_LIBRARY_PATH_DIRS}" TO_NATIVE_PATH_LIST FORGE_GPU_SHADER_TOOL_LIBRARY_PATH)
        if(CMAKE_HOST_WIN32)
            set(FORGE_GPU_SHADER_TOOL_PATH "PATH=${FORGE_GPU_SHADER_TOOL_LIBRARY_PATH};$ENV{PATH}")
            string(REPLACE ";" "\\;" FORGE_GPU_SHADER_TOOL_PATH "${FORGE_GPU_SHADER_TOOL_PATH}")
            set(FORGE_GPU_SHADER_TOOL_ENV "${CMAKE_COMMAND}" -E env "${FORGE_GPU_SHADER_TOOL_PATH}")
        elseif(CMAKE_HOST_APPLE)
            set(FORGE_GPU_SHADER_TOOL_ENV "${CMAKE_COMMAND}" -E env "DYLD_LIBRARY_PATH=${FORGE_GPU_SHADER_TOOL_LIBRARY_PATH}:$ENV{DYLD_LIBRARY_PATH}")
        elseif(CMAKE_HOST_UNIX)
            set(FORGE_GPU_SHADER_TOOL_ENV "${CMAKE_COMMAND}" -E env "LD_LIBRARY_PATH=${FORGE_GPU_SHADER_TOOL_LIBRARY_PATH}:$ENV{LD_LIBRARY_PATH}")
        endif()
    endif()

    add_custom_command(
        OUTPUT "${FORGE_GPU_GENERATED_STAMP}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${FORGE_GPU_GENERATED_SHADER_DIR}"
        COMMAND ${FORGE_GPU_SHADER_TOOL_ENV} "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate-shader-header.py"
            --shadercross "${FORGE_GPU_SHADERCROSS_EXECUTABLE}"
            --tint "${FORGE_GPU_TINT_EXECUTABLE}"
            --source-dir "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source"
            --output-dir "${FORGE_GPU_GENERATED_SHADER_DIR}"
            --layout-header "${FORGE_GPU_GENERATED_LAYOUT_HEADER}"
            --layout-source "${FORGE_GPU_GENERATED_LAYOUT_SOURCE}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${FORGE_GPU_GENERATED_STAMP}"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate-shader-header.py"
            ${FORGE_GPU_SHADER_SOURCES}
        VERBATIM
    )
    add_custom_target(forge-gpu-generate-shaders DEPENDS "${FORGE_GPU_GENERATED_STAMP}")
else()
    message(FATAL_ERROR "FORGE_GPU_GENERATE_SHADERS=OFF is only useful after providing generated headers.")
endif()

add_executable(forge-gpu-demo
    ${FORGE_GPU_SOURCES}
    "${FORGE_GPU_GENERATED_LAYOUT_SOURCE}"
)
add_dependencies(forge-gpu-demo forge-gpu-generate-shaders)
if(EMSCRIPTEN)
    add_dependencies(forge-gpu-demo forge-gpu-prewarm-emdawn)
endif()

if(NOT MSVC)
    set_source_files_properties(src/forge_gpu_assets.cpp PROPERTIES COMPILE_OPTIONS "-Wno-unused-function")
    set_source_files_properties(src/forge_gpu_lesson_28_ui.c PROPERTIES COMPILE_OPTIONS "-Wno-unused-function;-Wno-unused-variable")
endif()

set_source_files_properties("${FORGE_GPU_GENERATED_LAYOUT_SOURCE}" PROPERTIES GENERATED TRUE)
set_source_files_properties("${FORGE_GPU_GENERATED_LAYOUT_SOURCE}" PROPERTIES HEADER_FILE_ONLY FALSE)

target_include_directories(forge-gpu-demo PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cJSON"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/forge_gpu/common"
    "${FORGE_GPU_GENERATED_DIR}"
)
target_compile_definitions(forge-gpu-demo PRIVATE
    IMGUI_DISABLE_FILE_FUNCTIONS
    CJSON_HIDE_SYMBOLS
    SDLGPU_FORGE_GPU_DEFAULT_ASSET_ROOT="assets"
)
target_compile_features(forge-gpu-demo PRIVATE c_std_11 cxx_std_11)
forge_gpu_apply_no_fp_contract_options(forge-gpu-demo)
forge_gpu_apply_path_remap_options(forge-gpu-demo)

if(EMSCRIPTEN)
    if(FORGE_GPU_WEB_RELEASE AND NOT FORGE_GPU_WEB_SHELL_FILE)
        set(FORGE_GPU_WEB_SHELL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/forge_gpu_web_shell.html")
    endif()
    if(FORGE_GPU_WEB_RELEASE AND NOT EXISTS "${FORGE_GPU_WEB_SHELL_FILE}")
        message(FATAL_ERROR "FORGE_GPU_WEB_SHELL_FILE does not exist: ${FORGE_GPU_WEB_SHELL_FILE}")
    endif()
    file(RELATIVE_PATH FORGE_GPU_EMSCRIPTEN_ASSET_PRELOAD
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    )
    file(RELATIVE_PATH FORGE_GPU_EMSCRIPTEN_POST_JS
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/forge_gpu_emscripten_post.js"
    )
    if(TARGET SDL3::SDL3-static)
        target_link_libraries(forge-gpu-demo PRIVATE SDL3::SDL3-static)
    elseif(TARGET SDL3-static)
        target_link_libraries(forge-gpu-demo PRIVATE SDL3-static)
    else()
        message(FATAL_ERROR "Emscripten builds require a static SDL3 target.")
    endif()
    set_property(TARGET forge-gpu-demo APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/forge_gpu_emscripten_post.js"
    )
    set(FORGE_GPU_EMSCRIPTEN_LINK_OPTIONS
        --preload-file=${FORGE_GPU_EMSCRIPTEN_ASSET_PRELOAD}@/assets
        --post-js=${FORGE_GPU_EMSCRIPTEN_POST_JS}
        --use-port=emdawnwebgpu:cpp_bindings=false
        -sALLOW_MEMORY_GROWTH=1
        -sASYNCIFY=1
        -sASYNCIFY_STACK_SIZE=65536
        -sEXPORTED_RUNTIME_METHODS=FS
        -sEXIT_RUNTIME=0
    )
    if(FORGE_GPU_WEB_RELEASE)
        list(APPEND FORGE_GPU_EMSCRIPTEN_LINK_OPTIONS
            --shell-file=${FORGE_GPU_WEB_SHELL_FILE}
            -sASSERTIONS=0
        )
        set_property(TARGET forge-gpu-demo APPEND PROPERTY LINK_DEPENDS
            "${FORGE_GPU_WEB_SHELL_FILE}"
        )
    else()
        list(APPEND FORGE_GPU_EMSCRIPTEN_LINK_OPTIONS -sASSERTIONS=1)
    endif()
    target_link_options(forge-gpu-demo PRIVATE ${FORGE_GPU_EMSCRIPTEN_LINK_OPTIONS})
    set_target_properties(forge-gpu-demo PROPERTIES SUFFIX ".html")
    if(FORGE_GPU_WEB_RELEASE)
        add_custom_target(forge-gpu-demo-itch-zip
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/package-web-itch.py"
                --build-dir "${CMAKE_CURRENT_BINARY_DIR}"
                --stage-dir "${CMAKE_CURRENT_BINARY_DIR}/itch-stage"
                --output "${CMAKE_CURRENT_BINARY_DIR}/forge-gpu-demo-itch.zip"
            DEPENDS forge-gpu-demo
            VERBATIM
        )
    endif()
else()
    if(TARGET SDL3::SDL3-static)
        target_link_libraries(forge-gpu-demo PRIVATE SDL3::SDL3-static)
    elseif(TARGET SDL3-static)
        target_link_libraries(forge-gpu-demo PRIVATE SDL3-static)
    elseif(TARGET SDL3::SDL3)
        target_link_libraries(forge-gpu-demo PRIVATE SDL3::SDL3)
    else()
        message(FATAL_ERROR "No usable SDL3 target found.")
    endif()

    if(MSVC)
        target_compile_options(forge-gpu-demo PRIVATE /W3)
    else()
        target_compile_options(forge-gpu-demo PRIVATE -Wall -Wextra)
    endif()
endif()

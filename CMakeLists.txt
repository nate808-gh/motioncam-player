cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0111 NEW)
project(MCRAW_Player VERSION 0.5 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

message(STATUS "---------------------------------------------------------------------")
message(STATUS "Configuring ${PROJECT_NAME} (Version ${PROJECT_VERSION}) for Vulkan")
message(STATUS "  Source Directory: ${PROJECT_SOURCE_DIR}")
message(STATUS "  Binary Directory: ${CMAKE_BINARY_DIR}")
message(STATUS "---------------------------------------------------------------------")

# Find Qt6 Core, Gui, and Widgets modules
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# Next 1 line modified from original to compile on linux
find_package(Threads REQUIRED)

set(APP_ROOT_DIR        "${PROJECT_SOURCE_DIR}")
set(APP_EXTERNAL_DIR    "${APP_ROOT_DIR}/external")
set(APP_ASSETS_DIR      "${APP_ROOT_DIR}/assets")
set(APP_SHADERS_SRC_DIR "${APP_ROOT_DIR}/shaders")
set(APP_CMAKE_DIR       "${APP_ROOT_DIR}/cmake")

message(STATUS "Application Structure:")
message(STATUS "  Source files (.cpp):      ${APP_ROOT_DIR}/src (structured)")
message(STATUS "  Header files (.h):        ${APP_ROOT_DIR}/include (structured)")
message(STATUS "  External libraries:       ${APP_EXTERNAL_DIR}")
message(STATUS "  Asset files (fonts etc.): ${APP_ASSETS_DIR}")
message(STATUS "  Shader source files:      ${APP_SHADERS_SRC_DIR}")
message(STATUS "  Custom CMake modules:     ${APP_CMAKE_DIR}")

find_package(Vulkan REQUIRED)
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan SDK not found. Please ensure it is installed and discoverable.")
endif()
message(STATUS "Found Vulkan SDK:")
message(STATUS "  Libraries:    ${Vulkan_LIBRARIES}")
message(STATUS "  Include Dirs: ${Vulkan_INCLUDE_DIRS}")

set(GLM_SOURCE_DIR "${APP_EXTERNAL_DIR}/glm")
if(NOT EXISTS "${GLM_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "GLM CMakeLists.txt not found at ${GLM_SOURCE_DIR}.")
endif()
set(GLM_TEST_ENABLE OFF CACHE BOOL "Disable GLM tests" FORCE)
add_subdirectory(${GLM_SOURCE_DIR} ${CMAKE_BINARY_DIR}/glm_build EXCLUDE_FROM_ALL)
message(STATUS "Configured GLM from: ${GLM_SOURCE_DIR}")

set(GLFW_PATH "${APP_EXTERNAL_DIR}/glfw" CACHE PATH "Path to GLFW pre-built libraries and headers")
if(NOT EXISTS "${GLFW_PATH}/include/GLFW/glfw3.h")
    message(FATAL_ERROR "GLFW headers not found at ${GLFW_PATH}/include. Check GLFW_PATH.")
endif()
set(glfw3_DIR "${APP_CMAKE_DIR}" CACHE INTERNAL "Path to custom glfw3Config.cmake")
message(STATUS "Using pre-built GLFW from: ${GLFW_PATH}")

set(SDL2_PATH "${APP_EXTERNAL_DIR}/SDL2" CACHE PATH "Path to SDL2 development libraries")
if(NOT EXISTS "${SDL2_PATH}/include/SDL.h")
    message(FATAL_ERROR "SDL2 headers not found at ${SDL2_PATH}/include. Check SDL2_PATH.")
endif()
message(STATUS "Using pre-built SDL2 from: ${SDL2_PATH}")

set(VMA_INCLUDE_DIR "${APP_EXTERNAL_DIR}/vma")
if(NOT EXISTS "${VMA_INCLUDE_DIR}/vk_mem_alloc.h")
    message(FATAL_ERROR "vk_mem_alloc.h not found at ${VMA_INCLUDE_DIR}.")
endif()
message(STATUS "Found VMA header in: ${VMA_INCLUDE_DIR}")

if(WIN32)
    set(GLSLANG_VALIDATOR_EXECUTABLE_NAME "glslangValidator.exe")
else()
    set(GLSLANG_VALIDATOR_EXECUTABLE_NAME "glslangValidator")
endif()

if(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE)
    set(GLSLANG_VALIDATOR_CMD "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}")
    message(STATUS "Using glslangValidator from Vulkan SDK: ${GLSLANG_VALIDATOR_CMD}")
else()
    find_program(GLSLANG_VALIDATOR_PROGRAM NAMES ${GLSLANG_VALIDATOR_EXECUTABLE_NAME})
    if(GLSLANG_VALIDATOR_PROGRAM)
        set(GLSLANG_VALIDATOR_CMD "${GLSLANG_VALIDATOR_PROGRAM}")
        message(STATUS "Found glslangValidator in PATH: ${GLSLANG_VALIDATOR_CMD}")
    else()
        message(FATAL_ERROR "glslangValidator not found. Ensure it's in PATH or Vulkan SDK is configured correctly.")
    endif()
endif()

set(SHADER_COMPILED_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders_spv")
file(MAKE_DIRECTORY ${SHADER_COMPILED_DIR})

set(SHADER_FILES
        "${APP_SHADERS_SRC_DIR}/fullscreen_quad.vert"
        "${APP_SHADERS_SRC_DIR}/image_process.frag"
)
set(COMPILED_SHADER_OUTPUTS "")
foreach(SHADER_INPUT_FILE ${SHADER_FILES})
    get_filename_component(SHADER_BASENAME ${SHADER_INPUT_FILE} NAME)
    set(SHADER_OUTPUT_FILE "${SHADER_COMPILED_DIR}/${SHADER_BASENAME}.spv")
    if(NOT EXISTS ${SHADER_INPUT_FILE})
        message(FATAL_ERROR "Shader source file not found: ${SHADER_INPUT_FILE}")
    endif()
    add_custom_command(
            OUTPUT ${SHADER_OUTPUT_FILE}
            COMMAND ${GLSLANG_VALIDATOR_CMD} -V -o ${SHADER_OUTPUT_FILE} ${SHADER_INPUT_FILE}
            DEPENDS ${SHADER_INPUT_FILE}
            COMMENT "Compiling ${SHADER_BASENAME} to SPIR-V"
            VERBATIM
    )
    list(APPEND COMPILED_SHADER_OUTPUTS ${SHADER_OUTPUT_FILE})
endforeach()
add_custom_target(CompileShaders ALL DEPENDS ${COMPILED_SHADER_OUTPUTS}
        COMMENT "Compiling all GLSL shaders to SPIR-V"
)
message(STATUS "Shader compilation configured. Output to: ${SHADER_COMPILED_DIR}")

set(IMGUI_SOURCE_DIR "${APP_EXTERNAL_DIR}/imgui")
if(NOT EXISTS "${IMGUI_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "ImGui CMakeLists.txt not found at ${IMGUI_SOURCE_DIR}.")
endif()
set(IMGUI_BUILD_EXAMPLES OFF CACHE BOOL "Disable ImGui examples" FORCE)
add_subdirectory(${IMGUI_SOURCE_DIR} ${CMAKE_BINARY_DIR}/imgui_build EXCLUDE_FROM_ALL)
message(STATUS "Configured ImGui from: ${IMGUI_SOURCE_DIR}")

add_subdirectory(motioncam-decoder EXCLUDE_FROM_ALL)
message(STATUS "Configured motioncam-decoder subproject.")

set(APP_SOURCES
        src/App/AppInit.cpp
        src/App/AppLoop.cpp
        src/App/AppIO.cpp
        src/App/AppDecode.cpp
        src/App/AppInput.cpp
        src/App/AppCleanup.cpp

        src/Audio/AudioController.cpp
        src/Audio/AudioHelpers.cpp

        src/Decoder/DecoderWrapper.cpp

        src/Graphics/Renderer_VK.cpp
        src/Graphics/VulkanHelpers.cpp
        src/Graphics/ImageResource.cpp
        src/Graphics/Pipeline.cpp
        src/Graphics/Descriptor.cpp

        src/Gui/GuiSetup.cpp
        src/Gui/GuiRender.cpp
        src/Gui/GuiUtils.cpp
        src/Gui/GuiStyles.cpp

        src/Playback/PlaybackController.cpp

        src/Utils/DebugLog.cpp

        src/main.cpp
)

list(APPEND APP_SOURCES "${IMGUI_SOURCE_DIR}/backends/imgui_impl_glfw.cpp")
list(APPEND APP_SOURCES "${IMGUI_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")

message(STATUS "Application sources for executable: ${APP_SOURCES}")

# Ensure the Qt6 libraries are linked
add_executable(${PROJECT_NAME} "")
target_sources(${PROJECT_NAME} PRIVATE ${APP_SOURCES})

if(WIN32 AND EXISTS "${APP_ROOT_DIR}/icon.rc")
    target_sources(${PROJECT_NAME} PRIVATE "${APP_ROOT_DIR}/icon.rc")
    message(STATUS "Added Windows icon resource: ${APP_ROOT_DIR}/icon.rc")
endif()

# Link the Qt6 libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        motioncam_decoder
        imgui
        glm
        ${GLFW_LIB_PATH_VC_SPECIFIC}
        ${SDL2_LIB}
        ${Vulkan_LIBRARIES}
)

add_dependencies(${PROJECT_NAME} CompileShaders glm motioncam_decoder imgui)

target_compile_definitions(${PROJECT_NAME} PRIVATE SDL_MAIN_HANDLED)

if(MSVC AND CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
    message(STATUS "MSVC: Setting LINK_FLAGS /ENTRY:mainCRTStartup for ${PROJECT_NAME}")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/include/App"
        "${PROJECT_SOURCE_DIR}/include/Audio"
        "${PROJECT_SOURCE_DIR}/include/Decoder"
        "${PROJECT_SOURCE_DIR}/include/Graphics"
        "${PROJECT_SOURCE_DIR}/include/Gui"
        "${PROJECT_SOURCE_DIR}/include/Playback"
        "${PROJECT_SOURCE_DIR}/include/Utils"

        "${VMA_INCLUDE_DIR}"
        "${Vulkan_INCLUDE_DIRS}"
        "${GLFW_PATH}/include"
        "${SDL2_PATH}/include"

        "${APP_ROOT_DIR}/motioncam-decoder/lib/include"
        "${APP_ROOT_DIR}/motioncam-decoder/thirdparty"

        "${IMGUI_SOURCE_DIR}"
        "${IMGUI_SOURCE_DIR}/backends"
        "${APP_EXTERNAL_DIR}/tinydngwriter"
)

# Linux-specific options for GLFW and SDL2
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(SDL2_ARCH_SUFFIX "x64")
else()
    set(SDL2_ARCH_SUFFIX "x86")
endif()
set(SDL2_LIBRARY_DIR "${SDL2_PATH}/lib/${SDL2_ARCH_SUFFIX}")

set(GLFW_LIB_PATH_VC_SPECIFIC "/usr/lib/x86_64-linux-gnu/libglfw.so")
set(SDL2_LIB "SDL2")

target_link_libraries(${PROJECT_NAME} PRIVATE
        motioncam_decoder
        imgui
        glm
        ${GLFW_LIB_PATH_VC_SPECIFIC}
        ${SDL2_LIB}
        ${Vulkan_LIBRARIES}
)

set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${SHADER_COMPILED_DIR}"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders_spv"
        COMMENT "Copying compiled shaders to executable output directory"
        VERBATIM
)

if(EXISTS "${APP_ASSETS_DIR}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${APP_ASSETS_DIR}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
            COMMENT "Copying assets to executable output directory"
            VERBATIM
    )
    message(STATUS "POST_BUILD: Configured to copy ${APP_ASSETS_DIR} to $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets")
else()
    message(WARNING "Assets directory not found at ${APP_ASSETS_DIR}. Skipping asset copy.")
endif()

message(STATUS "---------------------------------------------------------------------")
message(STATUS "Final Configuration Summary:")
message(STATUS "  Target Executable: ${PROJECT_NAME}")
message(STATUS "  Output Directory:  ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_CFG_INTDIR}")
message(STATUS "  Output Executable: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_CFG_INTDIR}/${PROJECT_NAME}${CMAKE_EXECUTABLE_SUFFIX}")
message(STATUS "---------------------------------------------------------------------")

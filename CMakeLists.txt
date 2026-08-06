# General CMake. No subdirectory CMakeLists present
# 

cmake_minimum_required(VERSION 4.3.1 FATAL_ERROR)

set(NAME VkModelViewer)
project(${NAME})

# Permanently saving external dependencies 
set(FETCHCONTENT_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external" CACHE PATH "")
# Not checking updates if dpeendencies already there
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external")
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)
endif()

include(FetchContent)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()
set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)
set(CMAKE_INCLUDE_SYSTEM_FLAG_C "-I")
set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "-I")
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)



set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(DEFINED ENV{VULKAN_SDK})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{VULKAN_SDK}")
endif()

find_package(Vulkan REQUIRED)
find_package(SDL3 REQUIRED)

set(KTX_FEATURE_TESTS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TOOLS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_STATIC_LIBRARY ON CACHE BOOL "" FORCE)

# Dynamically loaded libraries
# GLM
FetchContent_Declare(
	glm 
	SYSTEM
	GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.3
	GIT_SHALLOW    TRUE
)
# ImGui
FetchContent_Declare(
	imgui
	SYSTEM
	GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.9b
	GIT_SHALLOW    TRUE
)
# FastGLTF
FetchContent_Declare(
	fastgltf
	SYSTEM
	GIT_REPOSITORY https://github.com/spnda/fastgltf
	GIT_TAG        v0.9.0
	GIT_SHALLOW    TRUE
)
# KTX-Software
FetchContent_Declare(
	ktx 
	SYSTEM
	GIT_REPOSITORY https://github.com/KhronosGroup/KTX-Software.git
    GIT_TAG        v4.4.2
	GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(glm imgui fastgltf ktx)

# Compiling static ImGui library as it doesn't have native CMake for FetchContent
add_library(imgui STATIC 
	${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
	${imgui_SOURCE_DIR}
	${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC SDL3::SDL3 Vulkan::Vulkan)

# In case subdirectories are needed
# add_subdirectory()

add_executable(Viewer main.cpp)

target_include_directories(Viewer PRIVATE 
	${Vulkan_INCLUDE_DIRS}
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_sources(Viewer PRIVATE
	src/core/Engine.h
	src/core/Engine.cpp 
	src/renderer/Renderer.h 
	src/renderer/Renderer.cpp
)

target_link_libraries(Viewer PRIVATE
	imgui
	glm::glm 
	fastgltf::fastgltf
	ktx 
	SDL3::SDL3
	Vulkan::Vulkan
)
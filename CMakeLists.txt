cmake_minimum_required(VERSION 3.10)
project(sky-gfx-sponza-demo)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
	set(CMAKE_MACOSX_BUNDLE YES)
endif()

set(RESOURCES
	assets
)

set(LIBS_FOLDER "libs")

add_executable(${PROJECT_NAME} src/main.cpp)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

# sky-gfx

add_subdirectory(lib/sky-gfx)
set_property(TARGET sky-gfx PROPERTY FOLDER ${LIBS_FOLDER})
target_link_libraries(${PROJECT_NAME} sky-gfx)

# glfw

set(GLFW_INSTALL OFF)
set(GLFW_BUILD_DOCS OFF)
add_subdirectory(lib/sky-gfx/examples/lib/glfw)
set_property(TARGET glfw PROPERTY FOLDER ${LIBS_FOLDER}/glfw)
set_property(TARGET update_mappings PROPERTY FOLDER ${LIBS_FOLDER}/glfw)
target_link_libraries(${PROJECT_NAME} glfw)

cmake_minimum_required(VERSION 3.20)
project(AggieXLMA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(GLEW CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(DuckDB CONFIG REQUIRED)
find_path(PORTABLE_FILE_DIALOGS_INCLUDE_DIRS "portable-file-dialogs.h")

include_directories(${CMAKE_SOURCE_DIR}/src)

set(SOURCES
    src/state.cpp
    src/main.cpp
)

if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES})
else()
    add_executable(${PROJECT_NAME} ${SOURCES})
endif()

target_include_directories(AggieXLMA PRIVATE 
    ${PORTABLE_FILE_DIALOGS_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    GLEW::GLEW
    glm::glm
    imgui::imgui
    $<IF:$<TARGET_EXISTS:duckdb>,duckdb,duckdb_static>
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE opengl32)
elseif(APPLE)
    find_library(OPENGL_LIBRARY OpenGL)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${OPENGL_LIBRARY})
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE GL)
endif()

if(WIN32)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
endif()
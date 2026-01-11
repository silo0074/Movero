cmake_minimum_required(VERSION 3.16)
project(FastCopier LANGUAGES CXX)
project(${PROJECT_NAME} VERSION 1.0.0)

# Pass the version to C++ as a macro
add_definitions(-DAPP_VERSION="${PROJECT_VERSION}")
add_definitions(-DAPP_NAME="${PROJECT_NAME}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON) # Automatically run MOC
set(CMAKE_AUTOUIC ON) # Automatically run UIC (if you use .ui files later)
set(CMAKE_AUTORCC ON) # Automatically run RCC (if you use .qrc files)

# Get project source directory
set(PROJECT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XXHASH REQUIRED libxxhash)

# It is safer to list headers and sources clearly
set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/CopyWorker.cpp
    src/MainWindow.ui
)

set(HEADERS
    src/MainWindow.h
    src/CopyWorker.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

if(CMAKE_BUILD_TYPE MATCHES "Debug")
    message(STATUS "Setting DEBUG_ENABLED to 1")
    target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG_ENABLED=1)
else()
    message(STATUS "Setting DEBUG_ENABLED to 0")
    target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG_ENABLED=0)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE 
    PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${XXHASH_LIBRARIES}
)

target_include_directories(${PROJECT_NAME} PRIVATE ${XXHASH_INCLUDE_DIRS})


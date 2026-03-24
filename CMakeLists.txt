cmake_minimum_required(VERSION 3.20)
project(sdbrdd VERSION 1.5.0 LANGUAGES C)

if(EXISTS "${CMAKE_BINARY_DIR}/generators/conan_toolchain.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_BINARY_DIR}/generators/conan_toolchain.cmake")
elseif(EXISTS "${CMAKE_BINARY_DIR}/build/generators/conan_toolchain.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_BINARY_DIR}/build/generators/conan_toolchain.cmake")
elseif(EXISTS "${CMAKE_BINARY_DIR}/build/Release/generators/conan_toolchain.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_BINARY_DIR}/build/Release/generators/conan_toolchain.cmake")
endif()

if(CMAKE_TOOLCHAIN_FILE)
    message(STATUS "Using Conan toolchain: ${CMAKE_TOOLCHAIN_FILE}")
    include(${CMAKE_TOOLCHAIN_FILE})
endif()

# Include the Harbour compiler configuration
include(cmake/HarbourCompiler.cmake)
include_directories(${HB_INC})

find_package(cfl-library CONFIG REQUIRED)
find_package(yapp-c-runtime CONFIG REQUIRED)
include_directories(${cfl-library_INCLUDE_DIRS})
include_directories(${yapp-c-runtime_INCLUDE_DIRS})

# OS Defines
if(WIN32)
    add_definitions(-D_SDB_OS_WIN_)
elseif(UNIX)
    add_definitions(-D_SDB_OS_LINUX_)
endif()

# Compiler Defines
if(MSVC)
    add_definitions(-D_SDB_CC_MSC_)
elseif(MINGW)
    add_definitions(-D_SDB_CC_MINGW64_)
elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    add_definitions(-D_SDB_CC_CLANG_)
elseif(CMAKE_C_COMPILER_ID MATCHES "Borland|Embarcadero")
    add_definitions(-D_SDB_CC_BCC_ -D_SDB_LIGHT_LOCK)
endif()

# Harbour/xHarbour Defines
# Logic to switch between Harbour and xHarbour if needed
option(USE_XHARBOUR "Build with xHarbour instead of Harbour" OFF)
if(USE_XHARBOUR)
    add_definitions(-D__XHB__ -D_SDB_CLP_XHB_)
    if(WIN32)
        add_definitions(-DHB_OS_WIN_32)
    endif()
else()
    add_definitions(-D_SDB_CLP_HB_ -D__HBR__)
endif()

# Architecture and Version
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_definitions(-D__ARCH__=64)
else()
    add_definitions(-D__ARCH__=32)
endif()
add_definitions(-D_SDB_VERSION_="${PROJECT_VERSION}")

add_subdirectory(sdb-rdd-lib)
add_subdirectory(sdb-ora-lib)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/sdb-rdd-headers/src/main/headers/"
    DESTINATION include
    FILES_MATCHING PATTERN "*.ch"
)

install(TARGETS sdb-rdd-lib sdb-ora-lib
    ARCHIVE DESTINATION lib
)

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_VERBATIM_VARIABLES YES)
include(CPack)

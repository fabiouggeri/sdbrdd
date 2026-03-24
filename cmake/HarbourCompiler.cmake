# HarbourCompiler.cmake

# This file defines the custom commands and settings required to compile the source files using the Harbour compiler.


# Find Harbour compiler and set environment
if(WIN32)
    find_program(HARBOUR_COMPILER
        NAMES harbour.exe
        DOC "Path to Harbour compiler executable"
    )
else()
    find_program(HARBOUR_COMPILER
        NAMES harbour
        DOC "Path to Harbour compiler executable"
    )

    if(NOT HARBOUR_COMPILER)
        message(FATAL_ERROR "Harbour compiler not found!")
    endif()
endif()

if(HARBOUR_COMPILER)
    cmake_path(GET HARBOUR_COMPILER PARENT_PATH HB_BIN_PATH)
    cmake_path(GET HB_BIN_PATH PARENT_PATH HB_ROOT)

    set(HB_ROOT ${HB_ROOT} CACHE PATH "Harbour installation root directory")
    if(EXISTS "${HB_ROOT}/include/harbour")
        set(HB_INC "${HB_ROOT}/include/harbour" CACHE PATH "Harbour include directory")
    else()
        set(HB_INC "${HB_ROOT}/include" CACHE PATH "Harbour include directory")
    endif()
    set(HB_LIB "${HB_ROOT}/lib" CACHE PATH "Harbour library directory")

    message(STATUS "Found Harbour compiler: ${HARBOUR_COMPILER}")
    message(STATUS "Harbour root directory: ${HB_ROOT}")
else()
    message(FATAL_ERROR "Harbour compiler not found! Please set HB_ROOT environment variable.")
endif()

# Validate Harbour compiler
execute_process(
    COMMAND ${HARBOUR_COMPILER} --version
    OUTPUT_VARIABLE HARBOUR_VERSION
    ERROR_VARIABLE HARBOUR_VERSION_ERROR
    RESULT_VARIABLE HARBOUR_VERSION_RESULT
)

if(NOT HARBOUR_VERSION_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to execute Harbour compiler: ${HARBOUR_VERSION_ERROR}")
endif()

# Function to compile Harbour/Clipper files
function(compile_harbour_files)
    set(options "")
    set(oneValueArgs OUTPUT_DIR)
    set(multiValueArgs SOURCE_FILES INCLUDE_DIRS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    file(MAKE_DIRECTORY ${ARG_OUTPUT_DIR})

   if(XHARBOUR)
      set(DEFINE_FLAGS "-D__XHB__")
   else()
      set(DEFINE_FLAGS "-D__HBR__")
   endif()

    set(INCLUDE_FLAGS "")
    foreach(INC_DIR ${ARG_INCLUDE_DIRS})
        list(APPEND INCLUDE_FLAGS "-I${INC_DIR}")
    endforeach()

    set(C_SOURCE_FILES "")
    foreach(SOURCE_FILE ${ARG_SOURCE_FILES})
        get_filename_component(FILE_NAME ${SOURCE_FILE} NAME_WE)
        set(OUTPUT_FILE "${ARG_OUTPUT_DIR}/${FILE_NAME}.c")
        list(APPEND C_SOURCE_FILES ${OUTPUT_FILE})

        add_custom_command(
            OUTPUT ${OUTPUT_FILE}
            COMMAND ${HARBOUR_COMPILER} ${SOURCE_FILE}
                -q -gc -n -w3
                ${DEFINE_FLAGS}
                ${INCLUDE_FLAGS}
                -o${OUTPUT_FILE}
            DEPENDS ${SOURCE_FILE}
            COMMENT "Compiling ${SOURCE_FILE} with Harbour"
        )
    endforeach()

    set(HARBOUR_C_SOURCES ${C_SOURCE_FILES} PARENT_SCOPE)
endfunction()
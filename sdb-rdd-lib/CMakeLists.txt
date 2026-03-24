set(CMAKE_C_STANDARD 99)

# Define source files
file(GLOB C_SOURCES "src/main/c/*.c")
file(GLOB PRG_SOURCES "src/main/clipper/*.prg")

# Set output directory for generated C files
set(HARBOUR_C_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated_c")

# Compile Harbour files
compile_harbour_files(
    SOURCE_FILES ${PRG_SOURCES}
    OUTPUT_DIR ${HARBOUR_C_OUTPUT_DIR}
    INCLUDE_DIRS 
        ${PROJECT_SOURCE_DIR}/sdb-rdd-headers/src/main/headers
        "${HB_INC}"
        ${cfl-library_INCLUDE_DIRS}
)

include_directories(
   ${PROJECT_SOURCE_DIR}/sdb-rdd-headers/src/main/headers
   ${HB_INC}
)

add_library(sdb-rdd-lib STATIC
            ${C_SOURCES}
            ${HARBOUR_C_SOURCES})

# target_include_directories(sdb-rdd-lib PRIVATE ${cfl_library_INCLUDE_DIRS})

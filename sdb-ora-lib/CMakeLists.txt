set(CMAKE_C_STANDARD 99)

add_library(sdb-ora-lib STATIC
            src/main/c/sdb_oci.c
            src/main/c/sdb_oci_lib.c
            src/main/c/sdb_oci_lob.c
            src/main/c/sdb_oci_rowid.c
            src/main/c/sdb_oci_stmt.c
            src/main/c/sdb_oci_var.c
            src/main/c/sdb_ora_expr.c)

include_directories(
   ${PROJECT_SOURCE_DIR}/sdb-rdd-headers/src/main/headers
   ${PROJECT_SOURCE_DIR}/sdb-ora-headers/src/main/headers
)

# target_include_directories(sdb-ora-lib PRIVATE ${cfl_library_INCLUDE_DIRS})

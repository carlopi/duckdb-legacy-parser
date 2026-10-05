cmake_minimum_required(VERSION 3.5)

set(TARGET_NAME legacy_parser)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})

set(CMAKE_CXX_STANDARD
    "17"
    CACHE STRING "C++ standard to enforce")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The Postgres-derived grammar DuckDB used up to 1.5, frozen
add_subdirectory(third_party/libpg_query)

include_directories(src/include)
include_directories(third_party/libpg_query/include)

file(GLOB_RECURSE TRANSFORMER_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/src/transform/*.cpp)

set(EXTENSION_SOURCES src/legacy_parser_extension.cpp src/transformer.cpp
                      ${TRANSFORMER_SOURCES})

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

target_link_libraries(${EXTENSION_NAME} duckdb_pg_query)
target_link_libraries(${LOADABLE_EXTENSION_NAME} duckdb_pg_query)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")

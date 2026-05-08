cmake_minimum_required(VERSION 3.24)
project(virtualvaultfs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)
find_package(SQLite3 REQUIRED)

add_library(virtualvaultfs_core
    src/app/App.cpp
    src/app/Config.cpp
    src/fuse/FuseMain.cpp
    src/fuse/Operations.cpp
    src/vfs/NodeStore.cpp
    src/vfs/PathResolver.cpp
    src/vfs/FileHandle.cpp
    src/storage/ObjectStore.cpp
    src/storage/BackendPaths.cpp
    src/db/Database.cpp
    src/db/Statement.cpp
    src/db/Transaction.cpp
    src/import/Importer.cpp
    src/util/Error.cpp
    src/util/FsUtil.cpp
    src/util/Logger.cpp
)

target_include_directories(virtualvaultfs_core PUBLIC
    src
    ${FUSE3_INCLUDE_DIRS}
)

target_link_libraries(virtualvaultfs_core PUBLIC
    ${FUSE3_LIBRARIES}
    SQLite::SQLite3
)

target_compile_options(virtualvaultfs_core PRIVATE
    ${FUSE3_CFLAGS_OTHER}
    -Wall -Wextra -Wpedantic
)

add_executable(virtualvaultfs src/main.cpp)
target_link_libraries(virtualvaultfs PRIVATE virtualvaultfs_core)

enable_testing()
add_subdirectory(tests)

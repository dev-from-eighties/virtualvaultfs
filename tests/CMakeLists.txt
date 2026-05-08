add_executable(virtualvaultfs_tests
    NodeStoreTests.cpp
    ObjectStoreTests.cpp
    PathResolverTests.cpp
)

target_link_libraries(virtualvaultfs_tests PRIVATE virtualvaultfs_core)
target_compile_options(virtualvaultfs_tests PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME virtualvaultfs_tests COMMAND virtualvaultfs_tests)

add_executable(virtualvaultfs_simulation
    Simulation.cpp
)

target_link_libraries(virtualvaultfs_simulation PRIVATE virtualvaultfs_core)
target_compile_options(virtualvaultfs_simulation PRIVATE -Wall -Wextra -Wpedantic)

project(tutorial-19-newton-timedep-heat)

add_executable(${PROJECT_NAME} main.cpp)
include (${hermes2d_SOURCE_DIR}/CMake.common)
set_common_target_properties(${PROJECT_NAME})
set(BIN ${PROJECT_BINARY_DIR}/${PROJECT_NAME})
add_test(tutorial-19-newton-timedep-heat ${BIN})
set_tests_properties(tutorial-19-newton-timedep-heat PROPERTIES LABELS slow)

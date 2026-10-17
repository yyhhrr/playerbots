cmake_minimum_required(VERSION 3.16)
project(playerbots_mage_aoe_tests LANGUAGES CXX)

enable_testing()

foreach(test_name IN ITEMS mage_aoe_geometry mage_aoe_policy need_roll_command)
    add_executable(${test_name}_test ${test_name}_test.cpp)
    target_include_directories(${test_name}_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
    target_compile_features(${test_name}_test PRIVATE cxx_std_20)
    add_test(NAME ${test_name} COMMAND ${test_name}_test)
endforeach()

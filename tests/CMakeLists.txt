# Every tests/test_*.cpp becomes its own QtTest executable registered with CTest.
file(GLOB VOCALINK_TEST_SUPPORT CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/support/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/support/*.h")
add_library(vocalink_test_support STATIC ${VOCALINK_TEST_SUPPORT})
target_include_directories(vocalink_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vocalink_test_support PUBLIC vocalink_core Qt6::Test Qt6::Network)

file(GLOB VOCALINK_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")
foreach(test_source IN LISTS VOCALINK_TESTS)
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE vocalink_test_support)
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES
        TIMEOUT 120
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;VOCALINK_TEST_DATA=${CMAKE_CURRENT_SOURCE_DIR}/data")
endforeach()

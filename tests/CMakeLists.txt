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

# Interface smoke tests: start the real app on each screen and fail on any QML
# warning. Each run gets a throwaway profile so your own settings are untouched.
if(TARGET VocalInk)
    set(VOCALINK_UI_SCREENS
        "talk" "board" "voices" "audio" "stream"
        "settings:appearance" "settings:access" "settings:shortcuts" "settings:speech"
        "settings:typing" "settings:providers" "settings:backup" "settings:about"
        "onboarding:0" "onboarding:1" "onboarding:2" "onboarding:3" "onboarding:4" "onboarding:5"
        "onboarding:6" "onboarding:7" "onboarding:8" "onboarding:9" "onboarding:10" "onboarding:11"
        "palette" "quicktype" "compact" "showtext" "miclive")
    foreach(screen IN LISTS VOCALINK_UI_SCREENS)
        string(REPLACE ":" "_" screen_id "${screen}")
        set(profile_dir "${CMAKE_CURRENT_BINARY_DIR}/ui-profiles/${screen_id}")
        set(skip_setup --no-onboarding)
        if(screen MATCHES "^onboarding")
            set(skip_setup)
        endif()
        add_test(NAME ui_${screen_id}
                 COMMAND ${CMAKE_COMMAND} -E env
                     QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software
                     $<TARGET_FILE:VocalInk> --profile ${profile_dir} ${skip_setup} --show ${screen} --demo
                     --screenshot ${CMAKE_CURRENT_BINARY_DIR}/ui-${screen_id}.png)
        set_tests_properties(ui_${screen_id} PROPERTIES TIMEOUT 60 LABELS ui)
    endforeach()
endif()

# A console executable, unlike the utility itself: the results have to reach a CI
# log. It links ime_core, so the objects under test are the ones the shipped
# executable is built from, compiled with the same flags -- /utf-8 in particular,
# without which the Chinese literals the diagnostic suite checks for would be
# mojibake in the test binary too, and the test would pass by agreeing with the bug.
add_executable(ime_tests
    main.cpp
    check.cpp
    check.h
    test_layout.cpp
    test_schedule.cpp
    test_rules.cpp
    test_presets.cpp
    test_diagnostic.cpp
    test_windows.cpp
    test_persist.cpp
    test_helper.cpp
)

target_link_libraries(ime_tests PRIVATE ime_core)

if(MSVC)
    target_compile_options(ime_tests PRIVATE /W4 /permissive-)
endif()

# Kept in step with kSuites in main.cpp. A name here that the binary does not know
# fails immediately with a usage message rather than silently testing nothing.
set(IME_TEST_SUITES
    layout
    schedule
    rules
    presets
    diagnostic
    window-identity
    layout-switch
    persist
    helper
)

foreach(suite IN LISTS IME_TEST_SUITES)
    add_test(NAME "${suite}" COMMAND ime_tests "${suite}")

    # 77 is CTest's convention. The window suites use it when the session cannot
    # create a window or change a keyboard layout, which a headless runner may not
    # be able to do; reporting that as a pass would be the worst outcome, since it
    # is indistinguishable from the mechanism working.
    set_tests_properties("${suite}" PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
endforeach()

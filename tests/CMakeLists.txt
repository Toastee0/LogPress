cmake_minimum_required(VERSION 3.16)

# ============================================================
# Integration tests for LogPilot tools
# ============================================================

# Find the built executables
set(LOGPARSE_EXE $<TARGET_FILE:logparse>)
set(LOGEXPLORE_EXE $<TARGET_FILE:logexplore>)
set(LOGFIX_EXE $<TARGET_FILE:logfix>)

# Sample log directory
set(SAMPLE_LOGS ${CMAKE_CURRENT_SOURCE_DIR}/sample-logs)

# All tests run from the project root so modes/ and fixes/ are found
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# --- logparse tests ---

add_test(NAME logparse_help
    COMMAND logparse --help)
set_tests_properties(logparse_help PROPERTIES
    PASS_REGULAR_EXPRESSION "logparse.*Semantic build log compression"
    WORKING_DIRECTORY ${PROJECT_ROOT})

add_test(NAME logparse_help_agent
    COMMAND logparse --help agent)
set_tests_properties(logparse_help_agent PROPERTIES
    PASS_REGULAR_EXPRESSION "AGENT SELF-UPDATE INSTRUCTIONS"
    WORKING_DIRECTORY ${PROJECT_ROOT})

add_test(NAME logparse_zephyr_log
    COMMAND logparse ${SAMPLE_LOGS}/zephyr-build-error.log)
set_tests_properties(logparse_zephyr_log PROPERTIES
    PASS_REGULAR_EXPRESSION "\\[LOGPARSE\\].*lines"
    WORKING_DIRECTORY ${PROJECT_ROOT})

add_test(NAME logparse_cmake_log
    COMMAND logparse ${SAMPLE_LOGS}/cmake-build-error.log)
set_tests_properties(logparse_cmake_log PROPERTIES
    PASS_REGULAR_EXPRESSION "\\[LOGPARSE\\].*lines"
    WORKING_DIRECTORY ${PROJECT_ROOT})

add_test(NAME logparse_zephyr_mode
    COMMAND logparse ${SAMPLE_LOGS}/zephyr-build-error.log --mode zephyr)
set_tests_properties(logparse_zephyr_mode PROPERTIES
    PASS_REGULAR_EXPRESSION "mode: zephyr"
    WORKING_DIRECTORY ${PROJECT_ROOT})

add_test(NAME logparse_json_output
    COMMAND logparse ${SAMPLE_LOGS}/cmake-build-error.log --json)
set_tests_properties(logparse_json_output PROPERTIES
    PASS_REGULAR_EXPRESSION "\"mode\":"
    WORKING_DIRECTORY ${PROJECT_ROOT})

add_test(NAME logparse_budget
    COMMAND logparse ${SAMPLE_LOGS}/zephyr-build-error.log --budget 50)
set_tests_properties(logparse_budget PROPERTIES
    PASS_REGULAR_EXPRESSION "\\[LOGPARSE\\]"
    WORKING_DIRECTORY ${PROJECT_ROOT})

# --- logexplore tests ---

add_test(NAME logexplore_help
    COMMAND logexplore --help)
set_tests_properties(logexplore_help PROPERTIES
    PASS_REGULAR_EXPRESSION "logexplore.*Structure discovery"
    WORKING_DIRECTORY ${PROJECT_ROOT})

add_test(NAME logexplore_help_agent
    COMMAND logexplore --help agent)
set_tests_properties(logexplore_help_agent PROPERTIES
    PASS_REGULAR_EXPRESSION "AGENT SELF-UPDATE INSTRUCTIONS"
    WORKING_DIRECTORY ${PROJECT_ROOT})

add_test(NAME logexplore_zephyr_log
    COMMAND logexplore ${SAMPLE_LOGS}/zephyr-build-error.log)
set_tests_properties(logexplore_zephyr_log PROPERTIES
    PASS_REGULAR_EXPRESSION "\\[LOGEXPLORE\\].*lines"
    WORKING_DIRECTORY ${PROJECT_ROOT})

add_test(NAME logexplore_show_freq
    COMMAND logexplore ${SAMPLE_LOGS}/zephyr-build-error.log --show-freq)
set_tests_properties(logexplore_show_freq PROPERTIES
    PASS_REGULAR_EXPRESSION "FREQUENCY TABLE"
    WORKING_DIRECTORY ${PROJECT_ROOT})

add_test(NAME logexplore_show_segments
    COMMAND logexplore ${SAMPLE_LOGS}/zephyr-build-error.log --show-segments)
set_tests_properties(logexplore_show_segments PROPERTIES
    PASS_REGULAR_EXPRESSION "SEGMENTS DETECTED"
    WORKING_DIRECTORY ${PROJECT_ROOT})

add_test(NAME logexplore_suggest_mode
    COMMAND logexplore ${SAMPLE_LOGS}/cmake-build-error.log --suggest-mode)
set_tests_properties(logexplore_suggest_mode PROPERTIES
    PASS_REGULAR_EXPRESSION "\\[mode\\]"
    WORKING_DIRECTORY ${PROJECT_ROOT})

# --- logfix tests ---

add_test(NAME logfix_help
    COMMAND logfix --help)
set_tests_properties(logfix_help PROPERTIES
    PASS_REGULAR_EXPRESSION "logfix.*Fix memory"
    WORKING_DIRECTORY ${PROJECT_ROOT})

add_test(NAME logfix_help_agent
    COMMAND logfix --help agent)
set_tests_properties(logfix_help_agent PROPERTIES
    PASS_REGULAR_EXPRESSION "AGENT SELF-UPDATE INSTRUCTIONS"
    WORKING_DIRECTORY ${PROJECT_ROOT})

add_test(NAME logfix_stats
    COMMAND logfix --stats)
set_tests_properties(logfix_stats PROPERTIES
    PASS_REGULAR_EXPRESSION "LOGFIX STATS"
    WORKING_DIRECTORY ${PROJECT_ROOT})

add_test(NAME logfix_validate
    COMMAND logfix --validate)
set_tests_properties(logfix_validate PROPERTIES
    PASS_REGULAR_EXPRESSION "LOGFIX VALIDATE"
    WORKING_DIRECTORY ${PROJECT_ROOT})

add_test(NAME logfix_query
    COMMAND logfix --query "depends on undefined node")
set_tests_properties(logfix_query PROPERTIES
    PASS_REGULAR_EXPRESSION "\\[LOGFIX\\].*Query"
    WORKING_DIRECTORY ${PROJECT_ROOT})

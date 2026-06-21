if(NOT DEFINED PIE_EXE)
    message(FATAL_ERROR "PIE_EXE is required")
endif()
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "WORK_DIR is required")
endif()
if(NOT DEFINED KIND)
    message(FATAL_ERROR "KIND is required")
endif()
if(NOT DEFINED PACKAGE_NAME)
    message(FATAL_ERROR "PACKAGE_NAME is required")
endif()

set(PROJECT_DIR "${WORK_DIR}/${PACKAGE_NAME}")
file(REMOVE_RECURSE "${PROJECT_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

execute_process(
    COMMAND "${PIE_EXE}" new "${KIND}" "${PROJECT_DIR}"
    RESULT_VARIABLE new_result
    OUTPUT_VARIABLE new_stdout
    ERROR_VARIABLE new_stderr
)
if(NOT new_result EQUAL 0)
    message(FATAL_ERROR "pie new ${KIND} failed: ${new_stdout}${new_stderr}")
endif()

if(NOT EXISTS "${PROJECT_DIR}/pie.toml")
    message(FATAL_ERROR "pie new ${KIND} did not create pie.toml")
endif()
if(NOT EXISTS "${PROJECT_DIR}/src")
    message(FATAL_ERROR "pie new ${KIND} did not create src/")
endif()
if(NOT EXISTS "${PROJECT_DIR}/tests")
    message(FATAL_ERROR "pie new ${KIND} did not create tests/")
endif()

file(READ "${PROJECT_DIR}/pie.toml" manifest)
string(FIND "${manifest}" "name = \"${PACKAGE_NAME}\"" name_pos)
if(name_pos EQUAL -1)
    message(FATAL_ERROR "pie.toml does not contain expected package name")
endif()
string(FIND "${manifest}" "type = \"${KIND}\"" kind_pos)
if(kind_pos EQUAL -1)
    message(FATAL_ERROR "pie.toml does not contain expected package kind")
endif()

if(KIND STREQUAL "app")
    if(NOT EXISTS "${PROJECT_DIR}/src/main.pie")
        message(FATAL_ERROR "pie new app did not create src/main.pie")
    endif()
    execute_process(
        COMMAND "${PIE_EXE}" run
        WORKING_DIRECTORY "${PROJECT_DIR}"
        RESULT_VARIABLE run_result
        OUTPUT_VARIABLE run_stdout
        ERROR_VARIABLE run_stderr
    )
    if(NOT run_result EQUAL 0)
        message(FATAL_ERROR "generated app did not run: ${run_stdout}${run_stderr}")
    endif()
    string(FIND "${run_stdout}" "hello from ${PACKAGE_NAME}" hello_pos)
    if(hello_pos EQUAL -1)
        message(FATAL_ERROR "generated app output was unexpected: ${run_stdout}")
    endif()
elseif(KIND STREQUAL "lib")
    if(NOT EXISTS "${PROJECT_DIR}/src/lib.pie")
        message(FATAL_ERROR "pie new lib did not create src/lib.pie")
    endif()
else()
    message(FATAL_ERROR "unknown KIND ${KIND}")
endif()

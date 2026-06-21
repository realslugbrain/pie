if(NOT DEFINED PIE_EXE)
    message(FATAL_ERROR "PIE_EXE is required")
endif()
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "WORK_DIR is required")
endif()

set(PROJECT_DIR "${WORK_DIR}/deps_app")
file(REMOVE_RECURSE "${PROJECT_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

execute_process(
    COMMAND "${PIE_EXE}" new app "${PROJECT_DIR}"
    RESULT_VARIABLE new_result
    OUTPUT_VARIABLE new_stdout
    ERROR_VARIABLE new_stderr
)
if(NOT new_result EQUAL 0)
    message(FATAL_ERROR "pie new app failed: ${new_stdout}${new_stderr}")
endif()

execute_process(
    COMMAND "${PIE_EXE}" add http
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE add_http_result
    OUTPUT_VARIABLE add_http_stdout
    ERROR_VARIABLE add_http_stderr
)
if(NOT add_http_result EQUAL 0)
    message(FATAL_ERROR "pie add http failed: ${add_http_stdout}${add_http_stderr}")
endif()

execute_process(
    COMMAND "${PIE_EXE}" add json@1.2.3
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE add_json_result
    OUTPUT_VARIABLE add_json_stdout
    ERROR_VARIABLE add_json_stderr
)
if(NOT add_json_result EQUAL 0)
    message(FATAL_ERROR "pie add json@1.2.3 failed: ${add_json_stdout}${add_json_stderr}")
endif()

file(READ "${PROJECT_DIR}/pie.toml" manifest)
string(FIND "${manifest}" "[dependencies]" deps_pos)
if(deps_pos EQUAL -1)
    message(FATAL_ERROR "pie add did not create [dependencies]")
endif()
string(FIND "${manifest}" "http = \"*\"" http_pos)
if(http_pos EQUAL -1)
    message(FATAL_ERROR "pie add http did not add wildcard dependency")
endif()
string(FIND "${manifest}" "json = \"1.2.3\"" json_pos)
if(json_pos EQUAL -1)
    message(FATAL_ERROR "pie add json@1.2.3 did not add pinned dependency")
endif()

execute_process(
    COMMAND "${PIE_EXE}" add http@2.0.0
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE update_http_result
    OUTPUT_VARIABLE update_http_stdout
    ERROR_VARIABLE update_http_stderr
)
if(NOT update_http_result EQUAL 0)
    message(FATAL_ERROR "pie add http@2.0.0 failed: ${update_http_stdout}${update_http_stderr}")
endif()

file(READ "${PROJECT_DIR}/pie.toml" manifest)
string(FIND "${manifest}" "http = \"2.0.0\"" http_updated_pos)
if(http_updated_pos EQUAL -1)
    message(FATAL_ERROR "pie add http@2.0.0 did not update dependency")
endif()
string(FIND "${manifest}" "http = \"*\"" http_old_pos)
if(NOT http_old_pos EQUAL -1)
    message(FATAL_ERROR "pie add http@2.0.0 left old wildcard dependency")
endif()
string(REGEX MATCHALL "http = " http_lines "${manifest}")
list(LENGTH http_lines http_count)
if(NOT http_count EQUAL 1)
    message(FATAL_ERROR "pie add http@2.0.0 created duplicate http dependency lines")
endif()

execute_process(
    COMMAND "${PIE_EXE}" remove json
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE remove_json_result
    OUTPUT_VARIABLE remove_json_stdout
    ERROR_VARIABLE remove_json_stderr
)
if(NOT remove_json_result EQUAL 0)
    message(FATAL_ERROR "pie remove json failed: ${remove_json_stdout}${remove_json_stderr}")
endif()

file(READ "${PROJECT_DIR}/pie.toml" manifest)
string(FIND "${manifest}" "json = \"1.2.3\"" json_removed_pos)
if(NOT json_removed_pos EQUAL -1)
    message(FATAL_ERROR "pie remove json left dependency in manifest")
endif()
string(FIND "${manifest}" "http = \"2.0.0\"" http_kept_pos)
if(http_kept_pos EQUAL -1)
    message(FATAL_ERROR "pie remove json removed unrelated dependency")
endif()

execute_process(
    COMMAND "${PIE_EXE}" remove missing_dep
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE remove_missing_result
    OUTPUT_VARIABLE remove_missing_stdout
    ERROR_VARIABLE remove_missing_stderr
)
if(remove_missing_result EQUAL 0)
    message(FATAL_ERROR "pie remove missing_dep unexpectedly succeeded")
endif()

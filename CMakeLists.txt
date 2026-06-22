cmake_minimum_required(VERSION 3.20)

set(CMAKE_ASM_NASM_OBJECT_FORMAT elf64)
project(Pie VERSION 0.1.0 LANGUAGES C ASM_NASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(PIE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(PIE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${PIE_GENERATED_DIR}")

file(GLOB_RECURSE PIE_FEATURE_MANIFESTS CONFIGURE_DEPENDS
    "${PIE_ROOT}/src/features/*/feature.json"
    "${PIE_ROOT}/src/features/*/*/feature.json"
)

set(PIE_GENERATED_REGISTRY_C "${PIE_GENERATED_DIR}/feature_registry.generated.c")
set(PIE_GENERATED_REGISTRY_H "${PIE_GENERATED_DIR}/feature_registry.generated.h")
set(PIE_GENERATED_PARSE_HOOKS_C "${PIE_GENERATED_DIR}/parse_hooks.generated.c")
set(PIE_GENERATED_SEMA_HOOKS_C "${PIE_GENERATED_DIR}/sema_hooks.generated.c")
set(PIE_GENERATED_LOWER_HOOKS_C "${PIE_GENERATED_DIR}/lower_hooks.generated.c")
set(PIE_GENERATED_ASM_HOOKS_C "${PIE_GENERATED_DIR}/asm_hooks.generated.c")

add_custom_command(
    OUTPUT "${PIE_GENERATED_REGISTRY_C}" "${PIE_GENERATED_REGISTRY_H}" "${PIE_GENERATED_PARSE_HOOKS_C}" "${PIE_GENERATED_SEMA_HOOKS_C}" "${PIE_GENERATED_LOWER_HOOKS_C}" "${PIE_GENERATED_ASM_HOOKS_C}"
    COMMAND "${Python3_EXECUTABLE}" "${PIE_ROOT}/tools/validators/validate_features.py"
            --root "${PIE_ROOT}/src/features"
    COMMAND "${Python3_EXECUTABLE}" "${PIE_ROOT}/tools/generators/gen_registry.py"
            --root "${PIE_ROOT}/src/features"
            --out-c "${PIE_GENERATED_REGISTRY_C}"
            --out-h "${PIE_GENERATED_REGISTRY_H}"
    COMMAND "${Python3_EXECUTABLE}" "${PIE_ROOT}/tools/generators/gen_parse_hooks.py"
            --root "${PIE_ROOT}/src/features"
            --out-c "${PIE_GENERATED_PARSE_HOOKS_C}"
    COMMAND "${Python3_EXECUTABLE}" "${PIE_ROOT}/tools/generators/gen_sema_hooks.py"
            --root "${PIE_ROOT}/src/features"
            --out-c "${PIE_GENERATED_SEMA_HOOKS_C}"
    COMMAND "${Python3_EXECUTABLE}" "${PIE_ROOT}/tools/generators/gen_lower_hooks.py"
            --root "${PIE_ROOT}/src/features"
            --out-c "${PIE_GENERATED_LOWER_HOOKS_C}"
    COMMAND "${Python3_EXECUTABLE}" "${PIE_ROOT}/tools/generators/gen_asm_hooks.py"
            --root "${PIE_ROOT}/src/features"
            --out-c "${PIE_GENERATED_ASM_HOOKS_C}"
    DEPENDS
            "${PIE_ROOT}/tools/validators/validate_features.py"
            "${PIE_ROOT}/tools/generators/gen_registry.py"
            "${PIE_ROOT}/tools/generators/gen_parse_hooks.py"
            "${PIE_ROOT}/tools/generators/gen_sema_hooks.py"
            "${PIE_ROOT}/tools/generators/gen_lower_hooks.py"
            "${PIE_ROOT}/tools/generators/gen_asm_hooks.py"
            ${PIE_FEATURE_MANIFESTS}
    VERBATIM
)

add_library(pie_core
    src/core/diag/diag.c
    src/core/source/source.c
    src/core/token/token.c
    src/core/lexer/lexer.c
    src/core/registry/registry.c
    src/core/modules/resolver.c
    src/core/modules/loader.c
    src/core/ast/ast.c
)

target_include_directories(pie_core PUBLIC
    "${PIE_ROOT}/include"
    "${PIE_GENERATED_DIR}"
)

target_compile_options(pie_core PRIVATE -Wall -Wextra -Wpedantic)

add_library(pie_frontend
    src/core/parser/parser.c
)

target_include_directories(pie_frontend PUBLIC
    "${PIE_ROOT}/include"
    "${PIE_GENERATED_DIR}"
)

target_link_libraries(pie_frontend PUBLIC pie_core)
target_compile_options(pie_frontend PRIVATE -Wall -Wextra -Wpedantic)

add_library(pie_middle
    src/core/ir/ir.c
    src/core/lower/lower.c
    src/core/sema/sema.c
    src/middle/borrowcheck/borrowcheck.c
)

target_include_directories(pie_middle PUBLIC
    "${PIE_ROOT}/include"
    "${PIE_GENERATED_DIR}"
)

target_link_libraries(pie_middle PUBLIC pie_core)
target_compile_options(pie_middle PRIVATE -Wall -Wextra -Wpedantic)

add_library(pie_backend_asm
    src/backend/asm/emit/asm_emit.c
)

target_include_directories(pie_backend_asm PUBLIC
    "${PIE_ROOT}/include"
    "${PIE_GENERATED_DIR}"
)

target_link_libraries(pie_backend_asm PUBLIC pie_core)
target_compile_options(pie_backend_asm PRIVATE -Wall -Wextra -Wpedantic)

file(GLOB_RECURSE PIE_FEATURE_SOURCES CONFIGURE_DEPENDS
    "${PIE_ROOT}/src/features/*.c"
)

add_library(pie_features
    "${PIE_GENERATED_REGISTRY_C}"
    "${PIE_GENERATED_PARSE_HOOKS_C}"
    "${PIE_GENERATED_SEMA_HOOKS_C}"
    "${PIE_GENERATED_LOWER_HOOKS_C}"
    "${PIE_GENERATED_ASM_HOOKS_C}"
    ${PIE_FEATURE_SOURCES}
)

target_include_directories(pie_features PUBLIC
    "${PIE_ROOT}/include"
    "${PIE_GENERATED_DIR}"
)

target_link_libraries(pie_features PUBLIC pie_core)
target_compile_options(pie_features PRIVATE -Wall -Wextra -Wpedantic)

add_library(pie_runtime STATIC
    runtime/c/runtime.c
    runtime/c/wideint.c
    runtime/c/widefloat.c
    runtime/c/pie_maybe.c
    runtime/asm/linux_x64/start.asm
)

target_include_directories(pie_runtime PUBLIC
    "${PIE_ROOT}/runtime/include"
    "${PIE_ROOT}/runtime/c"
)

target_link_libraries(pie_runtime PRIVATE m)

add_executable(piec
    src/main.c
    src/core/driver/driver.c
)

target_include_directories(piec PRIVATE
    "${PIE_ROOT}/include"
    "${PIE_GENERATED_DIR}"
)

target_link_libraries(piec PRIVATE
    pie_core
    pie_frontend
    pie_middle
    pie_backend_asm
    pie_features
)

target_compile_definitions(piec PRIVATE
    PIE_VERSION="${PROJECT_VERSION}"
    PIE_SOURCE_ROOT="${PIE_ROOT}"
)

target_compile_options(piec PRIVATE -Wall -Wextra -Wpedantic)

add_executable(pie
    src/main.c
    src/core/driver/driver.c
)

target_include_directories(pie PRIVATE
    "${PIE_ROOT}/include"
    "${PIE_GENERATED_DIR}"
)

target_link_libraries(pie PRIVATE
    pie_core
    pie_frontend
    pie_middle
    pie_backend_asm
    pie_features
)

target_compile_definitions(pie PRIVATE
    PIE_VERSION="${PROJECT_VERSION}"
    PIE_SOURCE_ROOT="${PIE_ROOT}"
)

target_compile_options(pie PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()

file(COPY "${PIE_ROOT}/tests/packages/hello"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/packages")
set(PIE_HELLO_PACKAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/packages/hello")

add_test(NAME piec_version COMMAND piec --version)
add_test(NAME piec_features COMMAND piec features)
add_test(
    NAME piec_emit_asm
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/hello.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/hello.generated.asm"
)
add_test(
    NAME piec_emit_ir
    COMMAND piec emit-ir "${PIE_ROOT}/tests/fixtures/arithmetic.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/arithmetic.generated.ir"
)
add_test(
    NAME piec_run_hello
    COMMAND piec run "${PIE_ROOT}/tests/fixtures/hello.pie"
)
set_tests_properties(piec_run_hello PROPERTIES
    PASS_REGULAR_EXPRESSION "hello from Pie"
)
add_test(
    NAME pie_cli_run_hello
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/hello.pie"
)
set_tests_properties(pie_cli_run_hello PROPERTIES
    PASS_REGULAR_EXPRESSION "hello from Pie"
)
add_test(
    NAME pie_cli_test_hello
    COMMAND pie test "${PIE_ROOT}/tests/fixtures/hello.pie"
)
set_tests_properties(pie_cli_test_hello PROPERTIES
    PASS_REGULAR_EXPRESSION "hello from Pie"
)
add_test(
    NAME pie_check_hello
    COMMAND pie check "${PIE_ROOT}/tests/fixtures/hello.pie"
)
add_test(
    NAME pie_check_package
    COMMAND pie check
)
set_tests_properties(pie_check_package PROPERTIES
    WORKING_DIRECTORY "${PIE_HELLO_PACKAGE_DIR}"
)
add_test(
    NAME pie_run_stdlib_require_json
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/stdlib_require_json.pie"
)
set_tests_properties(pie_run_stdlib_require_json PROPERTIES
    PASS_REGULAR_EXPRESSION "std-require=json"
)
add_test(
    NAME pie_run_package_require_local
    COMMAND pie run "${PIE_ROOT}/tests/packages/require_local/src/main.pie"
)
set_tests_properties(pie_run_package_require_local PROPERTIES
    PASS_REGULAR_EXPRESSION "sum=7"
)
add_test(
    NAME pie_run_module_structs
    COMMAND pie run "${PIE_ROOT}/tests/packages/module_structs/src/main.pie"
)
set_tests_properties(pie_run_module_structs PROPERTIES
    PASS_REGULAR_EXPRESSION "result=30"
)
add_test(
    NAME pie_run_module_recursive
    COMMAND pie run "${PIE_ROOT}/tests/packages/module_recursive/src/main.pie"
)
set_tests_properties(pie_run_module_recursive PROPERTIES
    PASS_REGULAR_EXPRESSION "triple=15"
)
add_test(
    NAME pie_unknown_std_require_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/unknown_std_require_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/unknown_std_require_error.asm"
)
add_test(
    NAME pie_missing_package_require_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/packages/require_local/src/missing_module_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/missing_package_require_error.asm"
)
add_test(
    NAME pie_package_build
    COMMAND pie build -o "${CMAKE_CURRENT_BINARY_DIR}/package_hello"
)
set_tests_properties(pie_package_build PROPERTIES
    WORKING_DIRECTORY "${PIE_HELLO_PACKAGE_DIR}"
)
add_test(
    NAME pie_package_build_default
    COMMAND pie build
)
set_tests_properties(pie_package_build_default PROPERTIES
    WORKING_DIRECTORY "${PIE_HELLO_PACKAGE_DIR}"
)
add_test(
    NAME pie_package_run
    COMMAND pie run
)
set_tests_properties(pie_package_run PROPERTIES
    WORKING_DIRECTORY "${PIE_HELLO_PACKAGE_DIR}"
    PASS_REGULAR_EXPRESSION "package-run=hello"
)
add_test(
    NAME pie_package_test
    COMMAND pie test
)
set_tests_properties(pie_package_test PROPERTIES
    WORKING_DIRECTORY "${PIE_HELLO_PACKAGE_DIR}"
    PASS_REGULAR_EXPRESSION "package-test=ok"
)
add_test(
    NAME pie_new_app
    COMMAND "${CMAKE_COMMAND}"
            -DPIE_EXE=$<TARGET_FILE:pie>
            -DWORK_DIR="${CMAKE_CURRENT_BINARY_DIR}/new-packages"
            -DKIND=app
            -DPACKAGE_NAME=fresh_app
            -P "${PIE_ROOT}/tests/cmake/pie_new_package.cmake"
)
add_test(
    NAME pie_new_lib
    COMMAND "${CMAKE_COMMAND}"
            -DPIE_EXE=$<TARGET_FILE:pie>
            -DWORK_DIR="${CMAKE_CURRENT_BINARY_DIR}/new-packages"
            -DKIND=lib
            -DPACKAGE_NAME=fresh_lib
            -P "${PIE_ROOT}/tests/cmake/pie_new_package.cmake"
)
add_test(
    NAME pie_add_remove_dependencies
    COMMAND "${CMAKE_COMMAND}"
            -DPIE_EXE=$<TARGET_FILE:pie>
            -DWORK_DIR="${CMAKE_CURRENT_BINARY_DIR}/dependency-packages"
            -P "${PIE_ROOT}/tests/cmake/pie_dependencies.cmake"
)
add_test(
    NAME pie_run_arithmetic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/arithmetic.pie"
)
set_tests_properties(pie_run_arithmetic PROPERTIES
    PASS_REGULAR_EXPRESSION "result=27"
)
add_test(
    NAME pie_run_bool
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/bool.pie"
)
set_tests_properties(pie_run_bool PROPERTIES
    PASS_REGULAR_EXPRESSION "flag=true"
)
add_test(
    NAME pie_run_primitives_extra
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/primitives_extra.pie"
)
set_tests_properties(pie_run_primitives_extra PROPERTIES
    PASS_REGULAR_EXPRESSION "char=P byte=255"
)
add_test(
    NAME pie_run_string_binding
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/string_binding.pie"
)
set_tests_properties(pie_run_string_binding PROPERTIES
    PASS_REGULAR_EXPRESSION "hello strings"
)
add_test(
    NAME pie_run_string_reassign_after_move
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/string_reassign_after_move.pie"
)
set_tests_properties(pie_run_string_reassign_after_move PROPERTIES
    PASS_REGULAR_EXPRESSION "Grace"
)
add_test(
    NAME pie_run_reference_string_arg
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/reference_string_arg.pie"
)
set_tests_properties(pie_run_reference_string_arg PROPERTIES
    PASS_REGULAR_EXPRESSION "after-ref=Simon"
)
add_test(
    NAME pie_run_reference_local_print
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/reference_local_print.pie"
)
set_tests_properties(pie_run_reference_local_print PROPERTIES
    PASS_REGULAR_EXPRESSION "ref-local=Grace"
)
add_test(
    NAME pie_run_mut_reference_arg_releases
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/mut_reference_arg_releases.pie"
)
set_tests_properties(pie_run_mut_reference_arg_releases PROPERTIES
    PASS_REGULAR_EXPRESSION "after-mut-ref=Grace"
)
add_test(
    NAME pie_run_mut_reference_assignment
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/mut_reference_assignment.pie"
)
set_tests_properties(pie_run_mut_reference_assignment PROPERTIES
    PASS_REGULAR_EXPRESSION "inside-mut-ref=Grace\nafter-mut-ref=Grace"
)
add_test(
    NAME pie_run_mut_to_shared_reference_coercion
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/mut_to_shared_reference_coercion.pie"
)
set_tests_properties(pie_run_mut_to_shared_reference_coercion PROPERTIES
    PASS_REGULAR_EXPRESSION "shared-from-mut=Simon\nreturned-shared=Simon\nafter-coercion=Grace"
)
add_test(
    NAME pie_run_return_reference_param
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/return_reference_param.pie"
)
set_tests_properties(pie_run_return_reference_param PROPERTIES
    PASS_REGULAR_EXPRESSION "still-owned=Simon"
)
add_test(
    NAME pie_run_region_scope
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/region_scope.pie"
)
set_tests_properties(pie_run_region_scope PROPERTIES
    PASS_REGULAR_EXPRESSION "after-region"
)
add_test(
    NAME pie_run_unsafe_raw_address
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/unsafe_raw_address.pie"
)
set_tests_properties(pie_run_unsafe_raw_address PROPERTIES
    PASS_REGULAR_EXPRESSION "unsafe-ok"
)
add_test(
    NAME pie_run_unsafe_raw_deref
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/unsafe_raw_deref.pie"
)
set_tests_properties(pie_run_unsafe_raw_deref PROPERTIES
    PASS_REGULAR_EXPRESSION "raw-deref=42"
)
add_test(
    NAME pie_run_unsafe_raw_deref_typed
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/unsafe_raw_deref_typed.pie"
)
set_tests_properties(pie_run_unsafe_raw_deref_typed PROPERTIES
    PASS_REGULAR_EXPRESSION "raw-typed=255 123456"
)
add_test(
    NAME pie_run_unsafe_raw_store
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/unsafe_raw_store.pie"
)
set_tests_properties(pie_run_unsafe_raw_store PROPERTIES
    PASS_REGULAR_EXPRESSION "raw-store=123456 255"
)
add_test(
    NAME pie_run_unsafe_raw_pointer_arithmetic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/unsafe_raw_pointer_arithmetic.pie"
)
set_tests_properties(pie_run_unsafe_raw_pointer_arithmetic PROPERTIES
    PASS_REGULAR_EXPRESSION "raw-arith=33"
)
add_test(
    NAME pie_run_unsafe_function_call
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/unsafe_function_call.pie"
)
set_tests_properties(pie_run_unsafe_function_call PROPERTIES
    PASS_REGULAR_EXPRESSION "unsafe-fn-ok"
)
add_test(
    NAME pie_run_comparison
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/comparison.pie"
)
set_tests_properties(pie_run_comparison PROPERTIES
    PASS_REGULAR_EXPRESSION "cmp=true true false"
)
add_test(
    NAME pie_run_logic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/logic.pie"
)
set_tests_properties(pie_run_logic PROPERTIES
    PASS_REGULAR_EXPRESSION "logic=true false ok"
)
add_test(
    NAME pie_run_logical_bang
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/logical_bang.pie"
)
set_tests_properties(pie_run_logical_bang PROPERTIES
    PASS_REGULAR_EXPRESSION "bang-not=true true"
)
add_test(
    NAME pie_run_if_else
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/if_else.pie"
)
set_tests_properties(pie_run_if_else PROPERTIES
    PASS_REGULAR_EXPRESSION "branch=big"
)
add_test(
    NAME pie_run_while_loop
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/while_loop.pie"
)
set_tests_properties(pie_run_while_loop PROPERTIES
    PASS_REGULAR_EXPRESSION "loop=10"
)
add_test(
    NAME pie_run_loop_control
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/loop_control.pie"
)
set_tests_properties(pie_run_loop_control PROPERTIES
    PASS_REGULAR_EXPRESSION "loop-control=12"
)
add_test(
    NAME pie_run_block_scope_shadow
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/block_scope_shadow.pie"
)
set_tests_properties(pie_run_block_scope_shadow PROPERTIES
    PASS_REGULAR_EXPRESSION "outer=1"
)
add_test(
    NAME pie_run_anonymous_block_scope
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/anonymous_block_scope.pie"
)
set_tests_properties(pie_run_anonymous_block_scope PROPERTIES
    PASS_REGULAR_EXPRESSION "block-outer=1"
)
add_test(
    NAME pie_run_function_call
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/function_call.pie"
)
set_tests_properties(pie_run_function_call PROPERTIES
    PASS_REGULAR_EXPRESSION "answer=42"
)
add_test(
    NAME pie_run_function_params
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/function_params.pie"
)
set_tests_properties(pie_run_function_params PROPERTIES
    PASS_REGULAR_EXPRESSION "params=42 7"
)
add_test(
    NAME pie_run_function_string_abi
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/function_string_abi.pie"
)
set_tests_properties(pie_run_function_string_abi PROPERTIES
    PASS_REGULAR_EXPRESSION "chosen=blue"
)
add_test(
    NAME pie_run_function_bool_return
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/function_bool_return.pie"
)
set_tests_properties(pie_run_function_bool_return PROPERTIES
    PASS_REGULAR_EXPRESSION "bool-return=true"
)
add_test(
    NAME pie_run_function_float_return
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/function_float_return.pie"
)
set_tests_properties(pie_run_function_float_return PROPERTIES
    PASS_REGULAR_EXPRESSION "float-return=1.500000"
)
add_test(
    NAME pie_run_function_void
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/function_void.pie"
)
set_tests_properties(pie_run_function_void PROPERTIES
    PASS_REGULAR_EXPRESSION "log=void"
)
add_test(
    NAME pie_run_function_explicit_void
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/function_explicit_void.pie"
)
set_tests_properties(pie_run_function_explicit_void PROPERTIES
    PASS_REGULAR_EXPRESSION "pong"
)
add_test(
    NAME pie_run_top_level_commas
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/top_level_commas.pie"
)
set_tests_properties(pie_run_top_level_commas PROPERTIES
    PASS_REGULAR_EXPRESSION "sum=3"
)
add_test(
    NAME pie_immutable_assignment_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/immutable_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/immutable_error.asm"
)
add_test(
    NAME pie_undefined_variable_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/undefined_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/undefined_error.asm"
)
add_test(
    NAME pie_duplicate_binding_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/duplicate_binding_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/duplicate_binding_error.asm"
)
add_test(
    NAME pie_bool_arithmetic_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/bool_arithmetic_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/bool_arithmetic_error.asm"
)
add_test(
    NAME pie_float_type_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/float_type_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/float_type_error.asm"
)
add_test(
    NAME pie_byte_type_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/byte_type_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/byte_type_error.asm"
)
add_test(
    NAME pie_logic_type_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/logic_type_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/logic_type_error.asm"
)
add_test(
    NAME pie_logical_bang_type_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/logical_bang_type_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/logical_bang_type_error.asm"
)
add_test(
    NAME pie_check_logical_bang_type_error
    COMMAND pie check "${PIE_ROOT}/tests/fixtures/logical_bang_type_error.pie"
)
add_test(
    NAME pie_binding_type_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/binding_type_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/binding_type_error.asm"
)
add_test(
    NAME pie_if_condition_type_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/if_condition_type_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/if_condition_type_error.asm"
)
add_test(
    NAME pie_while_condition_type_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/while_condition_type_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/while_condition_type_error.asm"
)
add_test(
    NAME pie_scope_leak_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/scope_leak_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/scope_leak_error.asm"
)
add_test(
    NAME pie_anonymous_block_scope_leak_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/anonymous_block_scope_leak_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/anonymous_block_scope_leak_error.asm"
)
add_test(
    NAME pie_break_outside_loop_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/break_outside_loop_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/break_outside_loop_error.asm"
)
add_test(
    NAME pie_continue_outside_loop_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/continue_outside_loop_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/continue_outside_loop_error.asm"
)
add_test(
    NAME pie_undefined_function_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/undefined_function_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/undefined_function_error.asm"
)
add_test(
    NAME pie_function_return_type_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/function_return_type_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/function_return_type_error.asm"
)
add_test(
    NAME pie_function_arity_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/function_arity_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/function_arity_error.asm"
)
add_test(
    NAME pie_function_arg_type_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/function_arg_type_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/function_arg_type_error.asm"
)
add_test(
    NAME pie_moved_value_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/moved_value_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/moved_value_error.asm"
)
add_test(
    NAME pie_move_after_move_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/move_after_move_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/move_after_move_error.asm"
)
add_test(
    NAME pie_function_arg_move_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/function_arg_move_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/function_arg_move_error.asm"
)
add_test(
    NAME pie_shared_then_mut_borrow_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/shared_then_mut_borrow_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/shared_then_mut_borrow_error.asm"
)
add_test(
    NAME pie_mut_then_shared_borrow_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/mut_then_shared_borrow_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/mut_then_shared_borrow_error.asm"
)
add_test(
    NAME pie_use_while_mut_borrowed_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/use_while_mut_borrowed_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/use_while_mut_borrowed_error.asm"
)
add_test(
    NAME pie_assign_while_borrowed_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/assign_while_borrowed_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/assign_while_borrowed_error.asm"
)
add_test(
    NAME pie_mut_borrow_immutable_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/mut_borrow_immutable_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/mut_borrow_immutable_error.asm"
)
add_test(
    NAME pie_mut_reference_assignment_type_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/mut_reference_assignment_type_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/mut_reference_assignment_type_error.asm"
)
add_test(
    NAME pie_region_escape_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/region_escape_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/region_escape_error.asm"
)
add_test(
    NAME pie_local_reference_escape_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/local_reference_escape_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/local_reference_escape_error.asm"
)
add_test(
    NAME pie_ref_alias_escape_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/ref_alias_escape_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/ref_alias_escape_error.asm"
)
add_test(
    NAME pie_struct_function_arg_move_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/struct_function_arg_move_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/struct_function_arg_move_error.asm"
)
add_test(
    NAME pie_struct_shared_then_mut_borrow_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/struct_shared_then_mut_borrow_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/struct_shared_then_mut_borrow_error.asm"
)
add_test(
    NAME pie_struct_mut_then_shared_borrow_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/struct_mut_then_shared_borrow_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/struct_mut_then_shared_borrow_error.asm"
)
add_test(
    NAME pie_struct_assign_while_borrowed_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/struct_assign_while_borrowed_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/struct_assign_while_borrowed_error.asm"
)
add_test(
    NAME pie_struct_use_while_mut_borrowed_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/struct_use_while_mut_borrowed_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/struct_use_while_mut_borrowed_error.asm"
)
add_test(
    NAME pie_struct_mut_borrow_immutable_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/struct_mut_borrow_immutable_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/struct_mut_borrow_immutable_error.asm"
)
add_test(
    NAME pie_struct_field_assign_while_borrowed_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/struct_field_assign_while_borrowed_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/struct_field_assign_while_borrowed_error.asm"
)
add_test(
    NAME pie_list_moved_value_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/list_moved_value_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/list_moved_value_error.asm"
)
add_test(
    NAME pie_list_function_arg_move_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/list_function_arg_move_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/list_function_arg_move_error.asm"
)
add_test(
    NAME pie_list_shared_then_mut_borrow_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/list_shared_then_mut_borrow_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/list_shared_then_mut_borrow_error.asm"
)
add_test(
    NAME pie_list_assign_while_borrowed_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/list_assign_while_borrowed_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/list_assign_while_borrowed_error.asm"
)
add_test(
    NAME pie_list_use_while_mut_borrowed_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/list_use_while_mut_borrowed_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/list_use_while_mut_borrowed_error.asm"
)
add_test(
    NAME pie_list_mut_borrow_immutable_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/list_mut_borrow_immutable_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/list_mut_borrow_immutable_error.asm"
)
add_test(
    NAME pie_list_index_assign_while_borrowed_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/list_index_assign_while_borrowed_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/list_index_assign_while_borrowed_error.asm"
)
add_test(
    NAME pie_list_index_assign_while_shared_borrowed_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/list_index_assign_while_shared_borrowed_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/list_index_assign_while_shared_borrowed_error.asm"
)
add_test(
    NAME pie_map_moved_value_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/map_moved_value_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/map_moved_value_error.asm"
)
add_test(
    NAME pie_map_function_arg_move_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/map_function_arg_move_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/map_function_arg_move_error.asm"
)
add_test(
    NAME pie_map_shared_then_mut_borrow_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/map_shared_then_mut_borrow_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/map_shared_then_mut_borrow_error.asm"
)
add_test(
    NAME pie_map_assign_while_borrowed_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/map_assign_while_borrowed_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/map_assign_while_borrowed_error.asm"
)
add_test(
    NAME pie_map_use_while_mut_borrowed_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/map_use_while_mut_borrowed_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/map_use_while_mut_borrowed_error.asm"
)
add_test(
    NAME pie_map_mut_borrow_immutable_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/map_mut_borrow_immutable_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/map_mut_borrow_immutable_error.asm"
)
add_test(
    NAME pie_map_index_assign_while_borrowed_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/map_index_assign_while_borrowed_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/map_index_assign_while_borrowed_error.asm"
)
add_test(
    NAME pie_map_index_assign_while_shared_borrowed_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/map_index_assign_while_shared_borrowed_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/map_index_assign_while_shared_borrowed_error.asm"
)
add_test(
    NAME pie_run_list_reassign_after_move
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/list_reassign_after_move.pie"
)
set_tests_properties(pie_run_list_reassign_after_move PROPERTIES
    PASS_REGULAR_EXPRESSION "4"
)
add_test(
    NAME pie_run_map_reassign_after_move
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/map_reassign_after_move.pie"
)
set_tests_properties(pie_run_map_reassign_after_move PROPERTIES
    PASS_REGULAR_EXPRESSION "42"
)
add_test(
    NAME pie_run_list_borrow_valid
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/list_borrow_valid.pie"
)
set_tests_properties(pie_run_list_borrow_valid PROPERTIES
    PASS_REGULAR_EXPRESSION "1"
)
add_test(
    NAME pie_run_map_borrow_valid
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/map_borrow_valid.pie"
)
set_tests_properties(pie_run_map_borrow_valid PROPERTIES
    PASS_REGULAR_EXPRESSION "37"
)
add_test(
    NAME pie_raw_address_requires_unsafe_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/raw_address_requires_unsafe_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/raw_address_requires_unsafe_error.asm"
)
add_test(
    NAME pie_raw_deref_requires_unsafe_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/raw_deref_requires_unsafe_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/raw_deref_requires_unsafe_error.asm"
)
add_test(
    NAME pie_raw_pointer_pointee_type_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/raw_pointer_pointee_type_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/raw_pointer_pointee_type_error.asm"
)
add_test(
    NAME pie_raw_store_requires_unsafe_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/raw_store_requires_unsafe_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/raw_store_requires_unsafe_error.asm"
)
add_test(
    NAME pie_raw_pointer_arithmetic_requires_unsafe_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/raw_pointer_arithmetic_requires_unsafe_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/raw_pointer_arithmetic_requires_unsafe_error.asm"
)
add_test(
    NAME pie_unsafe_function_call_requires_unsafe_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/unsafe_function_call_requires_unsafe_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/unsafe_function_call_requires_unsafe_error.asm"
)
add_test(
    NAME pie_function_abi_slots_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/function_abi_slots_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/function_abi_slots_error.asm"
)
add_test(
    NAME pie_function_void_return_value_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/function_void_return_value_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/function_void_return_value_error.asm"
)
add_test(
    NAME pie_run_token_syntax
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/token_syntax.pie"
)
set_tests_properties(pie_run_token_syntax PROPERTIES
    PASS_REGULAR_EXPRESSION "token=-78"
)
add_test(
    NAME pie_lexer_unterminated_string_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/unterminated_string.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/unterminated_string.asm"
)
add_test(
    NAME pie_run_int_width
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/int_width.pie"
)
set_tests_properties(pie_run_int_width PROPERTIES
    PASS_REGULAR_EXPRESSION "42.*1000.*100000.*999999999"
)
add_test(
    NAME pie_run_int_auto
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/int_auto.pie"
)
set_tests_properties(pie_run_int_auto PROPERTIES
    PASS_REGULAR_EXPRESSION "7.*300.*50000.*9999999999"
)
add_test(
    NAME pie_run_float_width
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/float_width.pie"
)
set_tests_properties(pie_run_float_width PROPERTIES
    PASS_REGULAR_EXPRESSION "3.14.*2.718281"
)
add_test(
    NAME pie_run_int_width_arithmetic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/int_width_arithmetic.pie"
)
set_tests_properties(pie_run_int_width_arithmetic PROPERTIES
    PASS_REGULAR_EXPRESSION "30.*3000"
)
add_test(
    NAME pie_run_int_width_function
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/int_width_function.pie"
)
set_tests_properties(pie_run_int_width_function PROPERTIES
    PASS_REGULAR_EXPRESSION "300"
)
add_test(
    NAME pie_run_int_wide
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/int_wide.pie"
)
set_tests_properties(pie_run_int_wide PROPERTIES
    PASS_REGULAR_EXPRESSION "999999999999999999.*999999999999999998000000000000000001.*1000000000000000000"
)
add_test(
    NAME pie_run_float_wide
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/float_wide.pie"
)
set_tests_properties(pie_run_float_wide PROPERTIES
    PASS_REGULAR_EXPRESSION "3.14159.*6.28318"
)
add_test(
    NAME pie_run_tuple_swap_basic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/tuple_swap_basic.pie"
)
set_tests_properties(pie_run_tuple_swap_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "21"
)
add_test(
    NAME pie_run_tuple_swap_fib
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/tuple_swap.pie"
)
set_tests_properties(pie_run_tuple_swap_fib PROPERTIES
    PASS_REGULAR_EXPRESSION "354224848179261915075"
)
add_test(
    NAME pie_run_elif
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/elif.pie"
)
set_tests_properties(pie_run_elif PROPERTIES
    PASS_REGULAR_EXPRESSION "medium"
)
add_test(
    NAME pie_run_elif_chain
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/elif_chain.pie"
)
set_tests_properties(pie_run_elif_chain PROPERTIES
    PASS_REGULAR_EXPRESSION "medium"
)
add_test(
    NAME pie_run_elif_no_else
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/elif_no_else.pie"
)
set_tests_properties(pie_run_elif_no_else PROPERTIES
    PASS_REGULAR_EXPRESSION "medium"
)
add_test(
    NAME pie_run_for_range
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/for_range.pie"
)
set_tests_properties(pie_run_for_range PROPERTIES
    PASS_REGULAR_EXPRESSION "sum=10"
)
add_test(
    NAME pie_run_for_range_inclusive
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/for_range_inclusive.pie"
)
set_tests_properties(pie_run_for_range_inclusive PROPERTIES
    PASS_REGULAR_EXPRESSION "sum=15"
)
add_test(
    NAME pie_run_for_body
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/for_body.pie"
)
set_tests_properties(pie_run_for_body PROPERTIES
    PASS_REGULAR_EXPRESSION "i=0.*i=1.*i=2"
)
add_test(
    NAME pie_run_struct_basic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/struct_basic.pie"
)
set_tests_properties(pie_run_struct_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "10.*20.*30"
)
add_test(
    NAME pie_run_struct_field_compound
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/struct_field_compound.pie"
)
set_tests_properties(pie_run_struct_field_compound PROPERTIES
    PASS_REGULAR_EXPRESSION "field-compound=1"
)
add_test(
    NAME pie_run_string_concat
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/string_concat.pie"
)
set_tests_properties(pie_run_string_concat PROPERTIES
    PASS_REGULAR_EXPRESSION "Hello World"
)
add_test(
    NAME pie_run_null_literal
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/null_literal.pie"
)
set_tests_properties(pie_run_null_literal PROPERTIES
    PASS_REGULAR_EXPRESSION "x is 10"
)
add_test(
    NAME pie_run_tuple_basic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/tuple_basic.pie"
)
set_tests_properties(pie_run_tuple_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "1"
)
add_test(
    NAME pie_run_list_basic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/list_basic.pie"
)
set_tests_properties(pie_run_list_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "10.*20.*30"
)
add_test(
    NAME pie_run_enum_basic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/enum_basic.pie"
)
set_tests_properties(pie_run_enum_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "20"
)
add_test(
    NAME pie_run_match_basic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/match_basic.pie"
)
set_tests_properties(pie_run_match_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "30"
)
add_test(
    NAME pie_run_match_value
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/match_value.pie"
)
set_tests_properties(pie_run_match_value PROPERTIES
    PASS_REGULAR_EXPRESSION "30"
)
add_test(
    NAME pie_run_match_value_min
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/match_value_min.pie"
)
set_tests_properties(pie_run_match_value_min PROPERTIES
    PASS_REGULAR_EXPRESSION "2"
)
add_test(
    NAME pie_run_closure_basic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/closure_basic.pie"
)
set_tests_properties(pie_run_closure_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "7"
)
add_test(
    NAME pie_run_closure_capture
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/closure_capture.pie"
)
set_tests_properties(pie_run_closure_capture PROPERTIES
    PASS_REGULAR_EXPRESSION "42"
)
add_test(
    NAME pie_run_closure_call
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/closure_call.pie"
)
set_tests_properties(pie_run_closure_call PROPERTIES
    PASS_REGULAR_EXPRESSION "7"
)
add_test(
    NAME pie_run_closure_capture_call
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/closure_capture_call.pie"
)
set_tests_properties(pie_run_closure_capture_call PROPERTIES
    PASS_REGULAR_EXPRESSION "17"
)
add_test(
    NAME pie_run_cast_basic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/cast_basic.pie"
)
set_tests_properties(pie_run_cast_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "42.*123"
)
add_test(
    NAME pie_run_map_methods
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/map_methods.pie"
)
set_tests_properties(pie_run_map_methods PROPERTIES
    PASS_REGULAR_EXPRESSION "295"
)

add_test(
    NAME pie_run_maybe_bool
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/maybe_test.pie"
)
set_tests_properties(pie_run_maybe_bool PROPERTIES
    PASS_REGULAR_EXPRESSION "maybe-1="
)
add_test(
    NAME pie_run_closure_call_no_args
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/closure_call_no_args.pie"
)
set_tests_properties(pie_run_closure_call_no_args PROPERTIES
    PASS_REGULAR_EXPRESSION "result=42"
)
add_test(
    NAME pie_run_map_empty
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/map_empty.pie"
)
set_tests_properties(pie_run_map_empty PROPERTIES
    PASS_REGULAR_EXPRESSION "map-empty"
)
add_test(
    NAME pie_run_map_one
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/map_one.pie"
)
set_tests_properties(pie_run_map_one PROPERTIES
    PASS_REGULAR_EXPRESSION "map-one"
)
add_test(
    NAME pie_run_enum_payload
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/enum_payload_basic.pie"
)
set_tests_properties(pie_run_enum_payload PROPERTIES
    PASS_REGULAR_EXPRESSION "42"
)
add_test(
    NAME pie_run_map_put
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/map_put.pie"
)
set_tests_properties(pie_run_map_put PROPERTIES
    PASS_REGULAR_EXPRESSION "map-put=3"
)
add_test(
    NAME pie_run_list_index
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/list_index.pie"
)
set_tests_properties(pie_run_list_index PROPERTIES
    PASS_REGULAR_EXPRESSION "list-sum=60"
)
add_test(
    NAME pie_run_index_assign
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/index_assign.pie"
)
set_tests_properties(pie_run_index_assign PROPERTIES
    PASS_REGULAR_EXPRESSION "99\n77"
)
add_test(
    NAME pie_run_recursive_function
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/recursive_function.pie"
)
set_tests_properties(pie_run_recursive_function PROPERTIES
    PASS_REGULAR_EXPRESSION "fact5=120"
)
add_test(
    NAME pie_run_float_arithmetic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/float_arithmetic.pie"
)
set_tests_properties(pie_run_float_arithmetic PROPERTIES
    PASS_REGULAR_EXPRESSION "sum=5.14"
)
add_test(
    NAME pie_run_byte_arithmetic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/byte_arithmetic.pie"
)
set_tests_properties(pie_run_byte_arithmetic PROPERTIES
    PASS_REGULAR_EXPRESSION "byte=254"
)
add_test(
    NAME pie_run_tuple_mixed
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/tuple_mixed.pie"
)
set_tests_properties(pie_run_tuple_mixed PROPERTIES
    PASS_REGULAR_EXPRESSION "tuple-mixed"
)
add_test(
    NAME pie_run_function_helpers
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/function_helpers.pie"
)
set_tests_properties(pie_run_function_helpers PROPERTIES
    PASS_REGULAR_EXPRESSION "abs=42"
)
add_test(
    NAME pie_run_block_scope
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/block_scope.pie"
)
set_tests_properties(pie_run_block_scope PROPERTIES
    PASS_REGULAR_EXPRESSION "block-expr=11"
)
add_test(
    NAME pie_run_println_multi
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/println_multi.pie"
)
set_tests_properties(pie_run_println_multi PROPERTIES
    PASS_REGULAR_EXPRESSION "value=42 true"
)
add_test(
    NAME pie_run_for_collection
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/for_collection.pie"
)
set_tests_properties(pie_run_for_collection PROPERTIES
    PASS_REGULAR_EXPRESSION "total=150"
)
add_test(
    NAME pie_run_for_collection_simple
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/for_collection_simple.pie"
)
set_tests_properties(pie_run_for_collection_simple PROPERTIES
    PASS_REGULAR_EXPRESSION "3"
)
add_test(
    NAME pie_run_ternary_basic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/ternary_basic.pie"
)
set_tests_properties(pie_run_ternary_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "true=100 false=200"
)
add_test(
    NAME pie_run_bitwise_basic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/bitwise_basic.pie"
)
set_tests_properties(pie_run_bitwise_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "12 & 10 = 8.*12 \\| 10 = 14.*12 \\^ 10 = 6.*12 << 2 = 48.*12 >> 1 = 6.*~0 = -1"
)
add_test(
    NAME pie_run_struct_methods
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/struct_methods.pie"
)
set_tests_properties(pie_run_struct_methods PROPERTIES
    PASS_REGULAR_EXPRESSION "x = 10.*y = 25.*sum = 35"
)
add_test(
    NAME pie_run_if_expression_variants
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/if_expression_variants.pie"
)
set_tests_properties(pie_run_if_expression_variants PROPERTIES
    PASS_REGULAR_EXPRESSION "x=1 y=20 z=100 inline=yes"
)
add_test(
    NAME pie_run_pub_visibility
    COMMAND pie run "${PIE_ROOT}/tests/packages/module_pub_visibility/src/main.pie"
)
set_tests_properties(pie_run_pub_visibility PROPERTIES
    PASS_REGULAR_EXPRESSION "result=10"
)
add_test(
    NAME pie_pub_visibility_private_error
    COMMAND pie check "${PIE_ROOT}/tests/packages/module_pub_visibility/src/test_private.pie"
)
set_tests_properties(pie_pub_visibility_private_error PROPERTIES
    PASS_REGULAR_EXPRESSION "undefined function 'private_helper'"
)
add_test(
    NAME pie_run_export_visibility
    COMMAND pie run "${PIE_ROOT}/tests/packages/module_export_visibility/src/main.pie"
)
set_tests_properties(pie_run_export_visibility PROPERTIES
    PASS_REGULAR_EXPRESSION "result=15"
)
add_test(
    NAME pie_run_const_declaration
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/const_declaration.pie"
)
set_tests_properties(pie_run_const_declaration PROPERTIES
    PASS_REGULAR_EXPRESSION "x=100 y=hello z=true"
)
add_test(
    NAME pie_const_type_mismatch_error
    COMMAND pie check "${PIE_ROOT}/tests/fixtures/const_type_mismatch.pie"
)
set_tests_properties(pie_const_type_mismatch_error PROPERTIES
    PASS_REGULAR_EXPRESSION "error"
)
add_test(
    NAME pie_const_duplicate_name_error
    COMMAND pie check "${PIE_ROOT}/tests/fixtures/const_duplicate_name.pie"
)
set_tests_properties(pie_const_duplicate_name_error PROPERTIES
    PASS_REGULAR_EXPRESSION "already declared"
)
add_test(
    NAME pie_const_undefined_error
    COMMAND pie check "${PIE_ROOT}/tests/fixtures/const_undefined.pie"
)
set_tests_properties(pie_const_undefined_error PROPERTIES
    PASS_REGULAR_EXPRESSION "undefined variable 'UNDEFINED'"
)
add_test(
    NAME pie_run_string_interpolation
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/string_interpolation.pie"
)
set_tests_properties(pie_run_string_interpolation PROPERTIES
    PASS_REGULAR_EXPRESSION "hello, Simon.*name=Simon age=42.*Pi is ~3"
)
add_test(
    NAME pie_run_for_multi_var
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/for_multi_var.pie"
)
set_tests_properties(pie_run_for_multi_var PROPERTIES
    PASS_REGULAR_EXPRESSION "60"
)
add_test(
    NAME pie_run_power_operator
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/power_operator.pie"
)
set_tests_properties(pie_run_power_operator PROPERTIES
    PASS_REGULAR_EXPRESSION "1024;27;1;49;256;8"
)
add_test(
    NAME pie_run_inc_dec_operators
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/inc_dec_operators.pie"
)
set_tests_properties(pie_run_inc_dec_operators PROPERTIES
    PASS_REGULAR_EXPRESSION "5;6;7;7;7;6;5;5"
)
add_test(
    NAME pie_run_list_operations
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/list_operations.pie"
)
set_tests_properties(pie_run_list_operations PROPERTIES
    PASS_REGULAR_EXPRESSION "3;4;4;99;99;99;1"
)

add_test(
    NAME pie_run_option_result
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/option_result_basic.pie"
)
set_tests_properties(pie_run_option_result PROPERTIES
    PASS_REGULAR_EXPRESSION "42;-1;100;1"
)

add_test(
    NAME pie_run_if_let
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/if_let_test.pie"
)
set_tests_properties(pie_run_if_let PROPERTIES
    PASS_REGULAR_EXPRESSION "42;-1"
)

add_test(
    NAME pie_run_generic_fn_multi_type
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/generic_fn_multi_type.pie"
)
set_tests_properties(pie_run_generic_fn_multi_type PROPERTIES
    PASS_REGULAR_EXPRESSION "42;hello;2;1"
)

add_test(
    NAME pie_run_generic_comprehensive
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/generic_comprehensive.pie"
)
set_tests_properties(pie_run_generic_comprehensive PROPERTIES
    PASS_REGULAR_EXPRESSION "42;hello;42;world;true;10;foo"
)

add_test(
    NAME pie_run_generic_arithmetic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/generic_arithmetic.pie"
)
set_tests_properties(pie_run_generic_arithmetic PROPERTIES
    PASS_REGULAR_EXPRESSION "30;42;42"
)

add_test(
    NAME pie_run_generic_comparison
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/generic_comparison.pie"
)
set_tests_properties(pie_run_generic_comparison PROPERTIES
    PASS_REGULAR_EXPRESSION "20;10;true;false"
)
add_test(
    NAME pie_run_struct_user_methods
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/struct_user_methods.pie"
)
set_tests_properties(pie_run_struct_user_methods PROPERTIES
    PASS_REGULAR_EXPRESSION "Hello Alice!Hello Bob!Bob says hello"
)
add_test(
    NAME pie_run_closure_invocation
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/closure_invocation.pie"
)
set_tests_properties(pie_run_closure_invocation PROPERTIES
    PASS_REGULAR_EXPRESSION "7121530"
)
add_test(
    NAME pie_run_try_basic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/try_basic.pie"
)
set_tests_properties(pie_run_try_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "10/2=5"
)
add_test(
    NAME pie_run_try_keyword
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/try_keyword.pie"
)
set_tests_properties(pie_run_try_keyword PROPERTIES
    PASS_REGULAR_EXPRESSION "parsed=42"
)
add_test(
    NAME pie_run_map_index
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/map_index.pie"
)
set_tests_properties(pie_run_map_index PROPERTIES
    PASS_REGULAR_EXPRESSION "age=30\ncharlie=40\nalice-new=31"
)
add_test(
    NAME pie_run_thread_spawn_join
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/thread_spawn_join.pie"
)
set_tests_properties(pie_run_thread_spawn_join PROPERTIES
    PASS_REGULAR_EXPRESSION "child.*child.*done"
)
add_test(
    NAME pie_run_thread_mutex_basic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/thread_mutex_basic.pie"
)
set_tests_properties(pie_run_thread_mutex_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "locked\nunlocked"
)
add_test(
    NAME pie_run_thread_sleep
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/thread_sleep.pie"
)
set_tests_properties(pie_run_thread_sleep PROPERTIES
    PASS_REGULAR_EXPRESSION "slept"
)
add_test(
    NAME pie_run_thread_channel_basic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/thread_channel_basic.pie"
)
set_tests_properties(pie_run_thread_channel_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "42"
)
add_test(
    NAME pie_run_thread_channel_multi
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/thread_channel_multi.pie"
)
set_tests_properties(pie_run_thread_channel_multi PROPERTIES
    PASS_REGULAR_EXPRESSION "123"
)
add_test(
    NAME pie_run_thread_spawn_closure
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/thread_spawn_closure.pie"
)
set_tests_properties(pie_run_thread_spawn_closure PROPERTIES
    PASS_REGULAR_EXPRESSION "child.*child.*done"
)
add_test(
    NAME pie_thread_spawn_type_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/thread_spawn_type_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/thread_spawn_type_error.asm"
)
add_test(
    NAME pie_thread_join_type_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/thread_join_type_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/thread_join_type_error.asm"
)
add_test(
    NAME pie_thread_channel_send_type_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/thread_channel_send_type_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/thread_channel_send_type_error.asm"
)
add_test(
    NAME pie_borrowcheck_method_call_while_borrowed
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/borrowcheck_method_call_while_borrowed.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/borrowcheck_method_call_while_borrowed.asm"
)
add_test(
    NAME pie_borrowcheck_method_call_while_mut_borrowed
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/borrowcheck_method_call_while_mut_borrowed.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/borrowcheck_method_call_while_mut_borrowed.asm"
)
add_test(
    NAME pie_borrowcheck_double_move
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/borrowcheck_double_move.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/borrowcheck_double_move.asm"
)
add_test(
    NAME pie_borrowcheck_index_assign_while_shared_borrowed
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/borrowcheck_index_assign_while_shared_borrowed.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/borrowcheck_index_assign_while_shared_borrowed.asm"
)
add_test(
    NAME pie_borrowcheck_string_interp_moved
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/borrowcheck_string_interp_moved.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/borrowcheck_string_interp_moved.asm"
)
add_test(
    NAME pie_borrowcheck_ternary_moved
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/borrowcheck_ternary_moved.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/borrowcheck_ternary_moved.asm"
)

add_test(
    NAME pie_run_defer_basic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/defer_basic.pie"
)
set_tests_properties(pie_run_defer_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "start"
)

add_test(
    NAME pie_run_defer_early_return
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/defer_early_return.pie"
)
set_tests_properties(pie_run_defer_early_return PROPERTIES
    PASS_REGULAR_EXPRESSION "cleanup done"
)

add_test(
    NAME pie_run_string_methods
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/string_methods.pie"
)
set_tests_properties(pie_run_string_methods PROPERTIES
    PASS_REGULAR_EXPRESSION "HELLO, WORLD"
)

add_test(
    NAME pie_run_generic_fn_basic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/generic_fn_basic.pie"
)
set_tests_properties(pie_run_generic_fn_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "42"
)

add_test(
    NAME pie_run_generic_comparable
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/generic_comparable.pie"
)
set_tests_properties(pie_run_generic_comparable PROPERTIES
    PASS_REGULAR_EXPRESSION "20"
)

add_test(
    NAME pie_run_generic_constraints
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/generic_constraints.pie"
)
set_tests_properties(pie_run_generic_constraints PROPERTIES
    PASS_REGULAR_EXPRESSION "10"
)

add_test(
    NAME pie_run_generic_multi_type_params
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/generic_multi_type_params.pie"
)
set_tests_properties(pie_run_generic_multi_type_params PROPERTIES
    PASS_REGULAR_EXPRESSION "42"
)

add_test(
    NAME pie_run_struct_moved_value_error
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/struct_moved_value_error.pie"
)
set_tests_properties(pie_run_struct_moved_value_error PROPERTIES
    PASS_REGULAR_EXPRESSION "1"
)

add_test(
    NAME pie_run_struct_reassign_after_move
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/struct_reassign_after_move.pie"
)
set_tests_properties(pie_run_struct_reassign_after_move PROPERTIES
    PASS_REGULAR_EXPRESSION "Bob"
)

add_test(
    NAME pie_run_struct_borrow_valid
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/struct_borrow_valid.pie"
)
set_tests_properties(pie_run_struct_borrow_valid PROPERTIES
    PASS_REGULAR_EXPRESSION "Simon"
)

add_test(
    NAME pie_run_if_expression
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/if_expression.pie"
)
set_tests_properties(pie_run_if_expression PROPERTIES
    PASS_REGULAR_EXPRESSION "x=20"
)

add_test(
    NAME pie_run_if_expr_simple
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/if_expr_simple.pie"
)
set_tests_properties(pie_run_if_expr_simple PROPERTIES
    PASS_REGULAR_EXPRESSION "x=1"
)

add_test(
    NAME pie_run_logic_combo
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/logic_combo.pie"
)
set_tests_properties(pie_run_logic_combo PROPERTIES
    PASS_REGULAR_EXPRESSION "logic=false,true,true,true"
)

add_test(
    NAME pie_run_map_basic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/map_basic.pie"
)
set_tests_properties(pie_run_map_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "1"
)

add_test(
    NAME pie_run_function_custom_type
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/function_custom_type.pie"
)
set_tests_properties(pie_run_function_custom_type PROPERTIES
    PASS_REGULAR_EXPRESSION "x=10"
)

add_test(
    NAME pie_borrowcheck_closure_capture_moved
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/borrowcheck_closure_capture_moved.pie"
)
set_tests_properties(pie_borrowcheck_closure_capture_moved PROPERTIES
    PASS_REGULAR_EXPRESSION "ok"
)

add_test(
    NAME pie_borrowcheck_if_branch_merge_borrow
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/borrowcheck_if_branch_merge_borrow.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/borrowcheck_if_branch_merge_borrow.asm"
)
add_test(
    NAME pie_borrowcheck_index_while_shared_borrowed
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/borrowcheck_index_while_shared_borrowed.pie"
)
set_tests_properties(pie_borrowcheck_index_while_shared_borrowed PROPERTIES
    PASS_REGULAR_EXPRESSION "1"
)

add_test(
    NAME pie_borrowcheck_method_call_moved
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/borrowcheck_method_call_moved.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/borrowcheck_method_call_moved.asm"
)
add_test(
    NAME pie_struct_field_access_moved_error
    COMMAND piec emit-asm "${PIE_ROOT}/tests/fixtures/struct_field_access_moved_error.pie"
            -o "${CMAKE_CURRENT_BINARY_DIR}/struct_field_access_moved_error.asm"
)
add_test(
    NAME pie_try_error
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/try_error.pie"
)
add_test(
    NAME pie_run_type_alias_basic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/type_alias_basic.pie"
)
set_tests_properties(pie_run_type_alias_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "42"
)

add_test(
    NAME pie_run_assert_basic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/assert_basic.pie"
)
set_tests_properties(pie_run_assert_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "all assertions passed"
)

add_test(
    NAME pie_run_assert_fail
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/assert_fail.pie"
)
add_test(
    NAME pie_run_assert_eq_fail
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/assert_eq_fail.pie"
)
add_test(
    NAME pie_run_string_index
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/string_index.pie"
)
set_tests_properties(pie_run_string_index PROPERTIES
    PASS_REGULAR_EXPRESSION "string indexing works"
)

add_test(
    NAME pie_run_string_index_oob
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/string_index_oob.pie"
)
add_test(
    NAME pie_run_do_while_basic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/do_while_basic.pie"
)
set_tests_properties(pie_run_do_while_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "5"
)

add_test(
    NAME pie_run_do_while_control
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/do_while_control.pie"
)
set_tests_properties(pie_run_do_while_control PROPERTIES
    PASS_REGULAR_EXPRESSION "5"
)

add_test(
    NAME pie_run_format_basic
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/format_basic.pie"
)
set_tests_properties(pie_run_format_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "hello, world"
)

add_test(
    NAME pie_run_labelled_break
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/labelled_break.pie"
)
set_tests_properties(pie_run_labelled_break PROPERTIES
    PASS_REGULAR_EXPRESSION "done"
)

add_test(
    NAME pie_run_labelled_continue
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/labelled_continue.pie"
)
set_tests_properties(pie_run_labelled_continue PROPERTIES
    PASS_REGULAR_EXPRESSION "done"
)

add_test(
    NAME pie_run_labelled_nested_break
    COMMAND pie run "${PIE_ROOT}/tests/fixtures/labelled_nested_break.pie"
)
set_tests_properties(pie_run_labelled_nested_break PROPERTIES
    PASS_REGULAR_EXPRESSION "2"
)

set(PIE_TEST_FAILURE_OUTPUT_REGEX
    "pie: error:|(^|[\\r\\n])error:|fatal error|Segmentation fault|segmentation fault|AddressSanitizer|UndefinedBehaviorSanitizer|core dumped|Traceback"
)

set(PIE_TEST_EXPECTED_ERROR_OUTPUT_REGEX
    "pie: error:|(^|[\\r\\n])error:|undefined|mismatch|expected|requires|already declared|out of scope|borrow|moved|unsafe|unterminated|assert"
)

set(PIE_EXPECTED_PASS_TESTS
    piec_version
    piec_features
    piec_emit_asm
    piec_emit_ir
    piec_run_hello
    pie_cli_run_hello
    pie_cli_test_hello
    pie_check_hello
    pie_check_package
    pie_run_stdlib_require_json
    pie_run_package_require_local
    pie_run_module_structs
    pie_run_module_recursive
    pie_package_build
    pie_package_build_default
    pie_package_run
    pie_package_test
    pie_new_app
    pie_new_lib
    pie_add_remove_dependencies
    pie_run_arithmetic
    pie_run_bool
    pie_run_primitives_extra
    pie_run_string_binding
    pie_run_string_reassign_after_move
    pie_run_reference_string_arg
    pie_run_reference_local_print
    pie_run_mut_reference_arg_releases
    pie_run_mut_reference_assignment
    pie_run_mut_to_shared_reference_coercion
    pie_run_return_reference_param
    pie_run_region_scope
    pie_run_unsafe_raw_address
    pie_run_unsafe_raw_deref
    pie_run_unsafe_raw_deref_typed
    pie_run_unsafe_raw_store
    pie_run_unsafe_raw_pointer_arithmetic
    pie_run_unsafe_function_call
    pie_run_comparison
    pie_run_logic
    pie_run_logical_bang
    pie_run_if_else
    pie_run_while_loop
    pie_run_loop_control
    pie_run_block_scope_shadow
    pie_run_anonymous_block_scope
    pie_run_function_call
    pie_run_function_params
    pie_run_function_string_abi
    pie_run_function_bool_return
    pie_run_function_float_return
    pie_run_function_void
    pie_run_function_explicit_void
    pie_run_top_level_commas
    pie_run_list_reassign_after_move
    pie_run_map_reassign_after_move
    pie_run_list_borrow_valid
    pie_run_map_borrow_valid
    pie_run_token_syntax
    pie_run_int_width
    pie_run_int_auto
    pie_run_float_width
    pie_run_int_width_arithmetic
    pie_run_int_width_function
    pie_run_int_wide
    pie_run_float_wide
    pie_run_tuple_swap_basic
    pie_run_tuple_swap_fib
    pie_run_elif
    pie_run_elif_chain
    pie_run_elif_no_else
    pie_run_for_range
    pie_run_for_range_inclusive
    pie_run_for_body
    pie_run_struct_basic
    pie_run_string_concat
    pie_run_null_literal
    pie_run_tuple_basic
    pie_run_list_basic
    pie_run_enum_basic
    pie_run_match_basic
    pie_run_match_value
    pie_run_match_value_min
    pie_run_closure_basic
    pie_run_closure_capture
    pie_run_closure_call
    pie_run_closure_capture_call
    pie_run_cast_basic
    pie_run_map_methods
    pie_run_maybe_bool
    pie_run_closure_call_no_args
    pie_run_map_empty
    pie_run_map_one
    pie_run_enum_payload
    pie_run_map_put
    pie_run_list_index
    pie_run_index_assign
    pie_run_recursive_function
    pie_run_float_arithmetic
    pie_run_byte_arithmetic
    pie_run_tuple_mixed
    pie_run_function_helpers
    pie_run_block_scope
    pie_run_println_multi
    pie_run_for_collection
    pie_run_for_collection_simple
    pie_run_ternary_basic
    pie_run_bitwise_basic
    pie_run_struct_methods
    pie_run_if_expression_variants
    pie_run_pub_visibility
    pie_run_export_visibility
    pie_run_const_declaration
    pie_run_string_interpolation
    pie_run_for_multi_var
    pie_run_power_operator
    pie_run_inc_dec_operators
    pie_run_list_operations
    pie_run_option_result
    pie_run_if_let
    pie_run_generic_fn_multi_type
    pie_run_generic_comprehensive
    pie_run_generic_arithmetic
    pie_run_generic_comparison
    pie_run_struct_user_methods
    pie_run_closure_invocation
    pie_run_try_basic
    pie_run_try_keyword
    pie_run_map_index
    pie_run_thread_spawn_join
    pie_run_thread_mutex_basic
    pie_run_thread_sleep
    pie_run_thread_channel_basic
    pie_run_thread_channel_multi
    pie_run_thread_spawn_closure
    pie_run_defer_basic
    pie_run_defer_early_return
    pie_run_string_methods
    pie_run_generic_fn_basic
    pie_run_generic_comparable
    pie_run_generic_constraints
    pie_run_generic_multi_type_params
    pie_run_struct_moved_value_error
    pie_run_struct_reassign_after_move
    pie_run_struct_borrow_valid
    pie_run_if_expression
    pie_run_if_expr_simple
    pie_run_logic_combo
    pie_run_map_basic
    pie_run_function_custom_type
    pie_borrowcheck_closure_capture_moved
    pie_borrowcheck_index_while_shared_borrowed
    pie_run_type_alias_basic
    pie_run_assert_basic
    pie_run_string_index
    pie_run_do_while_basic
    pie_run_do_while_control
    pie_run_format_basic
    pie_run_labelled_break
    pie_run_labelled_continue
    pie_run_labelled_nested_break
)

set(PIE_EXPECTED_ERROR_TESTS
    pie_anonymous_block_scope_leak_error
    pie_assign_while_borrowed_error
    pie_binding_type_error
    pie_bool_arithmetic_error
    pie_borrowcheck_double_move
    pie_borrowcheck_if_branch_merge_borrow
    pie_borrowcheck_index_assign_while_shared_borrowed
    pie_borrowcheck_method_call_moved
    pie_borrowcheck_method_call_while_borrowed
    pie_borrowcheck_method_call_while_mut_borrowed
    pie_borrowcheck_string_interp_moved
    pie_borrowcheck_ternary_moved
    pie_break_outside_loop_error
    pie_byte_type_error
    pie_check_logical_bang_type_error
    pie_const_duplicate_name_error
    pie_const_type_mismatch_error
    pie_const_undefined_error
    pie_continue_outside_loop_error
    pie_duplicate_binding_error
    pie_float_type_error
    pie_function_abi_slots_error
    pie_function_arg_move_error
    pie_function_arg_type_error
    pie_function_arity_error
    pie_function_return_type_error
    pie_function_void_return_value_error
    pie_if_condition_type_error
    pie_immutable_assignment_error
    pie_lexer_unterminated_string_error
    pie_list_assign_while_borrowed_error
    pie_list_function_arg_move_error
    pie_list_index_assign_while_borrowed_error
    pie_list_index_assign_while_shared_borrowed_error
    pie_list_moved_value_error
    pie_list_mut_borrow_immutable_error
    pie_list_shared_then_mut_borrow_error
    pie_list_use_while_mut_borrowed_error
    pie_local_reference_escape_error
    pie_logic_type_error
    pie_logical_bang_type_error
    pie_map_assign_while_borrowed_error
    pie_map_function_arg_move_error
    pie_map_index_assign_while_borrowed_error
    pie_map_index_assign_while_shared_borrowed_error
    pie_map_moved_value_error
    pie_map_mut_borrow_immutable_error
    pie_map_shared_then_mut_borrow_error
    pie_map_use_while_mut_borrowed_error
    pie_missing_package_require_error
    pie_move_after_move_error
    pie_moved_value_error
    pie_mut_borrow_immutable_error
    pie_mut_reference_assignment_type_error
    pie_mut_then_shared_borrow_error
    pie_pub_visibility_private_error
    pie_raw_address_requires_unsafe_error
    pie_raw_deref_requires_unsafe_error
    pie_raw_pointer_arithmetic_requires_unsafe_error
    pie_raw_pointer_pointee_type_error
    pie_raw_store_requires_unsafe_error
    pie_ref_alias_escape_error
    pie_region_escape_error
    pie_run_assert_eq_fail
    pie_run_assert_fail
    pie_run_string_index_oob
    pie_scope_leak_error
    pie_shared_then_mut_borrow_error
    pie_struct_assign_while_borrowed_error
    pie_struct_field_access_moved_error
    pie_struct_field_assign_while_borrowed_error
    pie_struct_function_arg_move_error
    pie_struct_mut_borrow_immutable_error
    pie_struct_mut_then_shared_borrow_error
    pie_struct_shared_then_mut_borrow_error
    pie_struct_use_while_mut_borrowed_error
    pie_thread_channel_send_type_error
    pie_thread_join_type_error
    pie_thread_spawn_type_error
    pie_try_error
    pie_undefined_function_error
    pie_undefined_variable_error
    pie_unknown_std_require_error
    pie_unsafe_function_call_requires_unsafe_error
    pie_use_while_mut_borrowed_error
    pie_while_condition_type_error
)

set(PIE_EXPECTED_ERROR_TESTS_NEED_OUTPUT_CHECK
    pie_anonymous_block_scope_leak_error
    pie_assign_while_borrowed_error
    pie_binding_type_error
    pie_bool_arithmetic_error
    pie_borrowcheck_double_move
    pie_borrowcheck_if_branch_merge_borrow
    pie_borrowcheck_index_assign_while_shared_borrowed
    pie_borrowcheck_method_call_moved
    pie_borrowcheck_method_call_while_borrowed
    pie_borrowcheck_method_call_while_mut_borrowed
    pie_borrowcheck_string_interp_moved
    pie_borrowcheck_ternary_moved
    pie_break_outside_loop_error
    pie_byte_type_error
    pie_check_logical_bang_type_error
    pie_continue_outside_loop_error
    pie_duplicate_binding_error
    pie_float_type_error
    pie_function_abi_slots_error
    pie_function_arg_move_error
    pie_function_arg_type_error
    pie_function_arity_error
    pie_function_return_type_error
    pie_function_void_return_value_error
    pie_if_condition_type_error
    pie_immutable_assignment_error
    pie_lexer_unterminated_string_error
    pie_list_assign_while_borrowed_error
    pie_list_function_arg_move_error
    pie_list_index_assign_while_borrowed_error
    pie_list_index_assign_while_shared_borrowed_error
    pie_list_moved_value_error
    pie_list_mut_borrow_immutable_error
    pie_list_shared_then_mut_borrow_error
    pie_list_use_while_mut_borrowed_error
    pie_local_reference_escape_error
    pie_logic_type_error
    pie_logical_bang_type_error
    pie_map_assign_while_borrowed_error
    pie_map_function_arg_move_error
    pie_map_index_assign_while_borrowed_error
    pie_map_index_assign_while_shared_borrowed_error
    pie_map_moved_value_error
    pie_map_mut_borrow_immutable_error
    pie_map_shared_then_mut_borrow_error
    pie_map_use_while_mut_borrowed_error
    pie_missing_package_require_error
    pie_move_after_move_error
    pie_moved_value_error
    pie_mut_borrow_immutable_error
    pie_mut_reference_assignment_type_error
    pie_mut_then_shared_borrow_error
    pie_raw_address_requires_unsafe_error
    pie_raw_deref_requires_unsafe_error
    pie_raw_pointer_arithmetic_requires_unsafe_error
    pie_raw_pointer_pointee_type_error
    pie_raw_store_requires_unsafe_error
    pie_ref_alias_escape_error
    pie_region_escape_error
    pie_scope_leak_error
    pie_shared_then_mut_borrow_error
    pie_struct_assign_while_borrowed_error
    pie_struct_field_access_moved_error
    pie_struct_field_assign_while_borrowed_error
    pie_struct_function_arg_move_error
    pie_struct_mut_borrow_immutable_error
    pie_struct_mut_then_shared_borrow_error
    pie_struct_shared_then_mut_borrow_error
    pie_struct_use_while_mut_borrowed_error
    pie_thread_channel_send_type_error
    pie_thread_join_type_error
    pie_thread_spawn_type_error
    pie_undefined_function_error
    pie_undefined_variable_error
    pie_unknown_std_require_error
    pie_unsafe_function_call_requires_unsafe_error
    pie_use_while_mut_borrowed_error
    pie_while_condition_type_error
)

foreach(test_name IN LISTS PIE_EXPECTED_PASS_TESTS)
    if(TEST ${test_name})
        set_tests_properties(${test_name} PROPERTIES
            FAIL_REGULAR_EXPRESSION "${PIE_TEST_FAILURE_OUTPUT_REGEX}"
        )
    endif()
endforeach()

foreach(test_name IN LISTS PIE_EXPECTED_ERROR_TESTS_NEED_OUTPUT_CHECK)
    if(TEST ${test_name})
        set_tests_properties(${test_name} PROPERTIES
            PASS_REGULAR_EXPRESSION "${PIE_TEST_EXPECTED_ERROR_OUTPUT_REGEX}"
        )
    endif()
endforeach()

foreach(test_name IN LISTS PIE_EXPECTED_ERROR_TESTS)
    if(TEST ${test_name})
        get_property(has_pass_regex TEST ${test_name} PROPERTY PASS_REGULAR_EXPRESSION SET)
        if(has_pass_regex)
            set_tests_properties(${test_name} PROPERTIES
                WILL_FAIL FALSE
            )
        else()
            set_tests_properties(${test_name} PROPERTIES
                WILL_FAIL TRUE
            )
        endif()
    endif()
endforeach()

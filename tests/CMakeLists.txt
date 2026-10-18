add_executable(test_font unit/test_font.cpp)
target_link_libraries(test_font PRIVATE skrumi_core)
add_test(NAME unit_font COMMAND test_font ${PROJECT_SOURCE_DIR}/installer/assets/DejaVuSans.ttf)

# Native unit tests
add_executable(test_graphics unit/test_graphics.cpp)
target_link_libraries(test_graphics PRIVATE skrumi_core)
add_test(NAME unit_graphics COMMAND test_graphics)

add_executable(test_lexer unit/test_lexer.cpp)
target_link_libraries(test_lexer PRIVATE skrumi_core)
add_test(NAME unit_lexer COMMAND test_lexer)

add_executable(test_parser unit/test_parser.cpp)
target_link_libraries(test_parser PRIVATE skrumi_core)
add_test(NAME unit_parser COMMAND test_parser)

add_executable(test_interpreter unit/test_interpreter.cpp)
target_link_libraries(test_interpreter PRIVATE skrumi_core)
add_test(NAME unit_interpreter COMMAND test_interpreter)

add_executable(test_web unit/test_web.cpp)
target_link_libraries(test_web PRIVATE skrumi_core)
add_test(NAME unit_web COMMAND test_web)

add_executable(test_certificates unit/test_certificates.cpp)
target_include_directories(test_certificates PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_certificates PRIVATE mbedx509)
add_test(NAME unit_certificates COMMAND test_certificates)

add_executable(test_positions unit/test_positions.cpp)
target_link_libraries(test_positions PRIVATE skrumi_core)
add_test(NAME unit_positions COMMAND test_positions)

add_executable(test_semantic_analyzer unit/test_semantic_analyzer.cpp)
target_link_libraries(test_semantic_analyzer PRIVATE skrumi_core)
add_test(NAME unit_semantic_analyzer COMMAND test_semantic_analyzer)

add_executable(test_lsp_protocol
    unit/test_lsp_protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/lsp/Json.cpp
    ${CMAKE_SOURCE_DIR}/src/lsp/Transport.cpp
    ${CMAKE_SOURCE_DIR}/src/lsp/DocumentManager.cpp
    ${CMAKE_SOURCE_DIR}/src/lsp/LspServer.cpp
)
target_include_directories(test_lsp_protocol PRIVATE ${CMAKE_SOURCE_DIR}/src/lsp)
target_link_libraries(test_lsp_protocol PRIVATE skrumi_core)
add_test(NAME unit_lsp_protocol COMMAND test_lsp_protocol)

# Packaging is tested from a copied CLI with no stdlib, then with only the result
# in a clean recipient directory. Python is a test dependency, never a runtime one.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME module_resolution COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_module_resolution.py $<TARGET_FILE:skrumi> ${CMAKE_SOURCE_DIR})
set_tests_properties(module_resolution PROPERTIES TIMEOUT 90)
add_test(NAME system_checked COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_system_checked.py $<TARGET_FILE:skrumi>)
add_test(NAME android_packaging COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_android_packaging.py)
add_test(NAME lsp_catalog COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_lsp_catalog.py $<TARGET_FILE:skrumi-lsp> ${CMAKE_SOURCE_DIR})
set_tests_properties(lsp_catalog PROPERTIES TIMEOUT 30)
add_test(NAME lsp_edits COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_lsp_edits.py $<TARGET_FILE:skrumi-lsp>)
add_test(NAME cli_tools COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_cli_tools.py $<TARGET_FILE:skrumi>)
# Short programs with their exact output, errors and exit codes (test_programs.json).
add_test(NAME programs COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_programs.py $<TARGET_FILE:skrumi>)
set_tests_properties(programs PROPERTIES TIMEOUT 300 ENVIRONMENT "SKRUMI_HOME=${CMAKE_SOURCE_DIR}")
# Every benchmark once on a tiny input: the programs run and all languages agree.
add_test(NAME benchmarks_smoke
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/benchmarks/run.py --smoke --skrumi $<TARGET_FILE:skrumi>)
# The numbers on the site follow the code: version, builtins, modules, tests. Every
# build writes them into site/index.html; the test catches a page left behind.
add_custom_target(site_facts ALL
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/packaging/site_facts.py
            --build ${CMAKE_BINARY_DIR} --ctest ${CMAKE_CTEST_COMMAND} --write
    COMMENT "Updating the numbers on the site")
add_test(NAME site_facts COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/packaging/site_facts.py
         --build ${CMAKE_BINARY_DIR} --ctest ${CMAKE_CTEST_COMMAND})
# The playground shows examples/raycaster.skm from its own copy (the site is published
# from site/ alone): the two stay the same file, and the example passes skrumi check.
add_test(NAME examples_raycaster_copy COMMAND ${CMAKE_COMMAND} -E compare_files
         ${CMAKE_SOURCE_DIR}/examples/raycaster.skm ${CMAKE_SOURCE_DIR}/site/playground/examples/raycaster.skm)
add_test(NAME examples_raycaster_check COMMAND skrumi check ${CMAKE_SOURCE_DIR}/examples/raycaster.skm)
set_tests_properties(examples_raycaster_check PROPERTIES PASS_REGULAR_EXPRESSION "no problems found"
                     ENVIRONMENT "SKRUMI_HOME=${CMAKE_SOURCE_DIR}")
add_test(NAME release_asset_inventory COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_release_assets.py)
add_test(NAME packaging_versions COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_versions.py ${CMAKE_SOURCE_DIR})
add_executable(test_bundle unit/test_bundle.cpp)
target_link_libraries(test_bundle PRIVATE skrumi_bundle)
add_test(NAME standalone_bundle_format COMMAND test_bundle $<TARGET_FILE:skrumi>)
set_tests_properties(standalone_bundle_format PROPERTIES LABELS standalone)
add_test(NAME standalone_resources COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/standalone/test_resources.py $<TARGET_FILE:skrumi>)
set_tests_properties(standalone_resources PROPERTIES TIMEOUT 90 LABELS standalone)
add_test(NAME standalone_graphics
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/standalone/test_graphics.py $<TARGET_FILE:skrumi>)
set_tests_properties(standalone_graphics PROPERTIES TIMEOUT 90 LABELS standalone SKIP_RETURN_CODE 77)
add_test(NAME images
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/standalone/test_images.py $<TARGET_FILE:skrumi>)
set_tests_properties(images PROPERTIES TIMEOUT 120 LABELS standalone)
add_test(NAME launch_options
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/standalone/test_launch.py $<TARGET_FILE:skrumi>)
set_tests_properties(launch_options PROPERTIES TIMEOUT 120 LABELS standalone)
add_test(NAME window_modes
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/standalone/test_window_modes.py $<TARGET_FILE:skrumi>)
set_tests_properties(window_modes PROPERTIES TIMEOUT 120 LABELS standalone SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
add_test(NAME window_size
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/standalone/test_window_size.py $<TARGET_FILE:skrumi>)
set_tests_properties(window_size PROPERTIES TIMEOUT 90 LABELS standalone SKIP_RETURN_CODE 77)
add_test(NAME sound
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/standalone/test_sound.py $<TARGET_FILE:skrumi>)
set_tests_properties(sound PROPERTIES TIMEOUT 60 LABELS standalone)
add_test(NAME debug_adapter
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/standalone/test_debugger.py $<TARGET_FILE:skrumi>)
set_tests_properties(debug_adapter PROPERTIES TIMEOUT 120 LABELS standalone)
add_test(NAME ui_interaction
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/standalone/test_ui.py $<TARGET_FILE:skrumi>)
set_tests_properties(ui_interaction PROPERTIES TIMEOUT 120 LABELS standalone SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
set(STANDALONE_TESTS hello cli modules unicode_json environment errors corruption http_client http_server tcp)
foreach(T IN LISTS STANDALONE_TESTS)
    add_test(NAME standalone_${T}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/standalone/test_standalone.py $<TARGET_FILE:skrumi> ${T})
    set_tests_properties(standalone_${T} PROPERTIES TIMEOUT 60 LABELS standalone)
endforeach()

add_test(NAME standalone_https
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/standalone/test_tls.py $<TARGET_FILE:skrumi>)
set_tests_properties(standalone_https PROPERTIES TIMEOUT 90 LABELS standalone)

add_test(NAME standalone_https_server
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/standalone/test_https_server.py $<TARGET_FILE:skrumi>)
set_tests_properties(standalone_https_server PROPERTIES TIMEOUT 90 LABELS standalone)

# Common environment for Skrumi script tests
set(SKRUMI_TEST_ENV
    "SKRUMI_HOME=${CMAKE_SOURCE_DIR}"
    "SKRUMI_TEST_VALUE=works"
    "SKRUMI_LOG_LEVEL=info"
)

# Core smoke tests from original repo
set(SMOKE_TESTS
    core_smoke
    language_core
    control_flow
    stdlib_smoke
    json_unicode
    env_logging
)

foreach(T ${SMOKE_TESTS})
    add_test(NAME smoke_${T} COMMAND skrumi ${CMAKE_CURRENT_SOURCE_DIR}/${T}.skm)
    set_tests_properties(smoke_${T} PROPERTIES ENVIRONMENT "${SKRUMI_TEST_ENV}")
endforeach()

# Comprehensive regression tests
set(REGRESSION_TESTS
    test_syntax_types
    test_expressions
    test_variables_scope
    test_control_flow
    test_functions
    test_arrays
    test_strings
    test_math
    test_modules
    test_stdlib
    test_json_advanced
    test_files
    test_env
    test_semantics
    test_structures
    test_slots
    test_lambdas
    test_methods
    test_null
    test_enums
    test_typed_containers
    test_gc
    test_calls
    test_fast_paths
)

foreach(T ${REGRESSION_TESTS})
    add_test(NAME regression_${T} COMMAND skrumi ${CMAKE_CURRENT_SOURCE_DIR}/regression/${T}.skm)
    set_tests_properties(regression_${T} PROPERTIES ENVIRONMENT "${SKRUMI_TEST_ENV}")
endforeach()

# Arguments after the script path reach the program through os_args().
add_test(NAME regression_test_os COMMAND skrumi ${CMAKE_CURRENT_SOURCE_DIR}/regression/test_os.skm first "второй аргумент")
set_tests_properties(regression_test_os PROPERTIES ENVIRONMENT "${SKRUMI_TEST_ENV}")

# CLI flags tests
add_test(NAME cli_version COMMAND skrumi --version)
string(REPLACE "." "[.]" SKRUMI_VERSION_PATTERN "${PROJECT_VERSION}")
set_tests_properties(cli_version PROPERTIES PASS_REGULAR_EXPRESSION "Skrumi ${SKRUMI_VERSION_PATTERN}")

add_test(NAME cli_help COMMAND skrumi --help)
set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "https://github.com/SkrinVex/Skrumi")

add_test(NAME lsp_cli_version COMMAND skrumi-lsp --version)
set_tests_properties(lsp_cli_version PROPERTIES PASS_REGULAR_EXPRESSION "skrumi-lsp ${SKRUMI_VERSION_PATTERN}")

add_test(NAME lsp_cli_help COMMAND skrumi-lsp --help)
set_tests_properties(lsp_cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Language Server")

# HTTP server/client local integration test (POSIX)
if(NOT WIN32)
    find_program(BASH_EXECUTABLE bash)
    if(BASH_EXECUTABLE)
        add_test(NAME integration_http_local
            COMMAND ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/regression/test_http_local.sh $<TARGET_FILE:skrumi>
        )
        set_tests_properties(integration_http_local PROPERTIES
            ENVIRONMENT "${SKRUMI_TEST_ENV}"
            TIMEOUT 20
        )

        add_test(NAME integration_logging_levels
            COMMAND ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/regression/test_logging_levels.sh $<TARGET_FILE:skrumi>
        )
        set_tests_properties(integration_logging_levels PROPERTIES
            ENVIRONMENT "${SKRUMI_TEST_ENV}"
        )

        add_test(NAME integration_error_handling
            COMMAND ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/regression/test_error_handling.sh $<TARGET_FILE:skrumi>
        )
        set_tests_properties(integration_error_handling PROPERTIES
            ENVIRONMENT "${SKRUMI_TEST_ENV}"
        )
    endif()
endif()

# Examples and the standard library must stay valid Skrumi: checked without running.
file(GLOB SKRUMI_CHECKED_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/examples/*.skm
    ${CMAKE_SOURCE_DIR}/examples/*/*.skm
    ${CMAKE_SOURCE_DIR}/std/*.skm)
foreach(SOURCE ${SKRUMI_CHECKED_SOURCES})
    get_filename_component(SOURCE_NAME ${SOURCE} NAME_WE)
    get_filename_component(SOURCE_DIR ${SOURCE} DIRECTORY)
    get_filename_component(SOURCE_GROUP ${SOURCE_DIR} NAME)
    add_test(NAME check_${SOURCE_GROUP}_${SOURCE_NAME} COMMAND skrumi check ${SOURCE})
    set_tests_properties(check_${SOURCE_GROUP}_${SOURCE_NAME} PROPERTIES PASS_REGULAR_EXPRESSION "no problems found")
endforeach()

# Every Skrumi code block in the documentation passes `skrumi check`, and every
# builtin and std function is described in DOCUMENTATION.md.
add_test(NAME docs_examples COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_docs.py $<TARGET_FILE:skrumi> ${CMAKE_SOURCE_DIR})

# The website example over real HTTP, and a multi-file project through the language server.
add_test(NAME web_server_example COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_web_server.py $<TARGET_FILE:skrumi> ${CMAKE_SOURCE_DIR})
set_tests_properties(web_server_example PROPERTIES TIMEOUT 60)
add_test(NAME lsp_project COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_lsp_project.py $<TARGET_FILE:skrumi-lsp>)
set_tests_properties(lsp_project PROPERTIES TIMEOUT 60)

add_test(NAME rebranding COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_rebranding.py $<TARGET_FILE:skrumi>)
if(NOT WIN32)
    add_test(NAME rebranding_installation COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_installation.py $<TARGET_FILE:skrumi>)
endif()

add_test(NAME ui_progress_dialog_close
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/standalone/test_progress_dialog.py $<TARGET_FILE:skrumi>)
set_tests_properties(ui_progress_dialog_close PROPERTIES TIMEOUT 60 LABELS standalone SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)

add_test(NAME process_capture COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_process_capture.py $<TARGET_FILE:skrumi>)
set_tests_properties(process_capture PROPERTIES TIMEOUT 60)
add_test(NAME persistent_environment COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_persistent_environment.py $<TARGET_FILE:skrumi>)
set_tests_properties(persistent_environment PROPERTIES TIMEOUT 60)
add_test(NAME installer_transactions COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/installer/test_transactions.py $<TARGET_FILE:skrumi>)
set_tests_properties(installer_transactions PROPERTIES TIMEOUT 180 LABELS standalone)
add_test(NAME check_installer COMMAND skrumi check ${CMAKE_SOURCE_DIR}/installer/main.skm)
set_tests_properties(check_installer PROPERTIES PASS_REGULAR_EXPRESSION "no problems found")
add_test(NAME installer_gui COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/installer/test_gui.py $<TARGET_FILE:skrumi> $<TARGET_FILE:skrumi-lsp>)
set_tests_properties(installer_gui PROPERTIES TIMEOUT 180 LABELS standalone SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
add_test(NAME checked_archives COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_archives.py $<TARGET_FILE:skrumi>)
set_tests_properties(checked_archives PROPERTIES TIMEOUT 60)

add_test(NAME user_registry COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_user_registry.py $<TARGET_FILE:skrumi>)
set_tests_properties(user_registry PROPERTIES TIMEOUT 60)

add_test(NAME installer_gui_ru COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/installer/test_gui.py $<TARGET_FILE:skrumi> $<TARGET_FILE:skrumi-lsp> ru)
set_tests_properties(installer_gui_ru PROPERTIES TIMEOUT 180 LABELS standalone SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
add_test(NAME installer_translations COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/installer/test_translations.py $<TARGET_FILE:skrumi>)

add_test(NAME brand_audit COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_brand_audit.py)

add_test(NAME language_contracts COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_language_contracts.py $<TARGET_FILE:skrumi>)
set_tests_properties(language_contracts PROPERTIES TIMEOUT 120)

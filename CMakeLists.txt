cmake_minimum_required(VERSION 3.24)

project(XuyanForge VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

option(XUYANFORGE_BUILD_UI "Build the Qt Quick desktop application" ON)
option(XUYANFORGE_BUILD_TESTS "Build tests" ON)

# Qt's MinGW kit places common native libraries below <toolchain>/opt. Add that
# conventional prefix without persisting any developer-machine absolute path.
get_filename_component(XUYANFORGE_COMPILER_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(XUYANFORGE_TOOLCHAIN_ROOT "${XUYANFORGE_COMPILER_BIN}" DIRECTORY)
if(EXISTS "${XUYANFORGE_TOOLCHAIN_ROOT}/opt/include/sqlite3.h")
    list(PREPEND CMAKE_PREFIX_PATH "${XUYANFORGE_TOOLCHAIN_ROOT}/opt")
endif()
find_package(SQLite3 REQUIRED)

add_library(xuyan_domain STATIC
    libs/domain/src/hash.cpp
    libs/domain/src/character_blueprint.cpp
    libs/domain/src/character_instance.cpp
    libs/domain/src/branch_outcome.cpp
    libs/domain/src/evidence.cpp
    libs/domain/src/extraction_candidate.cpp
    libs/domain/src/extraction_job.cpp
    libs/domain/src/scenario.cpp
    libs/domain/src/simulation_session.cpp
    libs/domain/src/source_document.cpp
    libs/domain/src/provider_connection.cpp
    libs/domain/src/retrieval.cpp
    libs/domain/src/world_entity.cpp
    libs/domain/src/world_version.cpp
    libs/domain/src/world_graph.cpp
)
target_include_directories(xuyan_domain PUBLIC libs/domain/include)
target_compile_features(xuyan_domain PUBLIC cxx_std_20)

add_library(xuyan_engine STATIC
    libs/engine/src/mock_provider.cpp
    libs/engine/src/simulation_engine.cpp
)
target_include_directories(xuyan_engine PUBLIC libs/engine/include)
target_link_libraries(xuyan_engine PUBLIC xuyan_domain)

add_library(xuyan_providers STATIC
    libs/providers/src/model_protocol.cpp
    libs/providers/src/sse_parser.cpp
)
target_include_directories(xuyan_providers PUBLIC libs/providers/include)
target_link_libraries(xuyan_providers PUBLIC xuyan_domain xuyan_package)

add_library(xuyan_package STATIC
    libs/package/src/json.cpp
    libs/package/src/zip_archive.cpp
)
target_include_directories(xuyan_package PUBLIC libs/package/include)
target_link_libraries(xuyan_package PUBLIC xuyan_domain)

add_library(xuyan_storage_sqlite STATIC
    libs/storage_sqlite/src/workspace_repository.cpp
)
target_include_directories(xuyan_storage_sqlite PUBLIC libs/storage_sqlite/include)
target_link_libraries(xuyan_storage_sqlite PUBLIC xuyan_domain SQLite::SQLite3)

add_library(xuyan_application STATIC
    libs/application/src/branch_outcome_service.cpp
    libs/application/src/simulation_service.cpp
    libs/application/src/backup_service.cpp
    libs/application/src/evidence_service.cpp
    libs/application/src/candidate_service.cpp
    libs/application/src/extraction_job_service.cpp
    libs/application/src/mock_extraction_processor.cpp
    libs/application/src/remote_extraction_processor.cpp
    libs/application/src/character_service.cpp
    libs/application/src/character_instance_service.cpp
    libs/application/src/package_service.cpp
    libs/application/src/provider_connection_service.cpp
    libs/application/src/provider_generation_service.cpp
    libs/application/src/retrieval_service.cpp
    libs/application/src/source_import_service.cpp
    libs/application/src/source_text_decoder.cpp
    libs/application/src/workspace_service.cpp
    libs/application/src/world_version_service.cpp
    libs/application/src/world_graph_service.cpp
)
target_include_directories(xuyan_application PUBLIC libs/application/include)
target_link_libraries(xuyan_application PUBLIC xuyan_domain xuyan_engine xuyan_package xuyan_providers xuyan_storage_sqlite)

if(WIN32)
    add_library(xuyan_platform_credentials STATIC
        libs/platform_native/src/credential_store_windows.cpp
    )
    target_include_directories(xuyan_platform_credentials PUBLIC libs/platform_native/include)
    target_include_directories(xuyan_platform_credentials PRIVATE libs/application/include libs/domain/include)
    target_link_libraries(xuyan_platform_credentials PRIVATE advapi32)
else()
    add_library(xuyan_platform_credentials STATIC
        libs/platform_native/src/credential_store_unavailable.cpp
    )
    target_include_directories(xuyan_platform_credentials PUBLIC libs/platform_native/include)
    target_include_directories(xuyan_platform_credentials PRIVATE libs/application/include libs/domain/include)
endif()

if(XUYANFORGE_BUILD_UI)
    set(CMAKE_AUTOMOC ON)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2)
    qt_standard_project_setup(REQUIRES 6.5)
    qt_policy(SET QTP0004 NEW)

    add_library(xuyan_providers_qt STATIC
        libs/providers_qt/src/qt_provider_transport.cpp
    )
    target_include_directories(xuyan_providers_qt PUBLIC libs/providers_qt/include)
    target_link_libraries(xuyan_providers_qt PUBLIC xuyan_application Qt6::Core Qt6::Network)

    qt_add_executable(xuyanforge_app
        apps/native/main.cpp
        ui/viewmodels/simulation_view_model.cpp
        ui/viewmodels/simulation_view_model.h
        ui/viewmodels/workspace_view_model.cpp
        ui/viewmodels/workspace_view_model.h
        ui/viewmodels/workspace_catalog_view_model.cpp
        ui/viewmodels/workspace_catalog_view_model.h
        ui/viewmodels/source_view_model.cpp
        ui/viewmodels/source_view_model.h
        ui/viewmodels/character_view_model.cpp
        ui/viewmodels/character_view_model.h
        ui/viewmodels/package_view_model.cpp
        ui/viewmodels/package_view_model.h
        ui/viewmodels/provider_view_model.cpp
        ui/viewmodels/provider_view_model.h
        ui/viewmodels/extraction_job_view_model.cpp
        ui/viewmodels/extraction_job_view_model.h
        ui/viewmodels/candidate_review_view_model.cpp
        ui/viewmodels/candidate_review_view_model.h
        ui/viewmodels/world_views_view_model.cpp
        ui/viewmodels/world_views_view_model.h
    )
    target_include_directories(xuyanforge_app PRIVATE ui/viewmodels)
    target_link_libraries(xuyanforge_app PRIVATE
        xuyan_application xuyan_platform_credentials xuyan_providers_qt Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2
    )

    add_executable(xuyanforge_provider_cli tools/provider_cli.cpp)
    target_link_libraries(xuyanforge_provider_cli PRIVATE
        xuyan_application xuyan_platform_credentials xuyan_providers_qt Qt6::Core Qt6::Network
    )
    qt_add_qml_module(xuyanforge_app
        URI XuyanForge
        VERSION 1.0
        QML_FILES ui/qml/Main.qml
    )

    install(TARGETS xuyanforge_app
        BUNDLE DESTINATION .
        RUNTIME DESTINATION bin
    )
endif()

if(XUYANFORGE_BUILD_TESTS)
    enable_testing()
    add_executable(core_tests tests/core_tests.cpp tests/legacy_fixture.cpp libs/application/src/demo_world_service.cpp)
    target_link_libraries(core_tests PRIVATE xuyan_application xuyan_platform_credentials xuyan_providers)
    add_test(NAME core_tests COMMAND core_tests)
    add_executable(stress_tests tests/stress_tests.cpp)
    target_link_libraries(stress_tests PRIVATE xuyan_storage_sqlite)
    add_test(NAME stress_tests COMMAND stress_tests)
    if(WIN32)
        set_tests_properties(core_tests PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${XUYANFORGE_COMPILER_BIN}")
        set_tests_properties(stress_tests PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${XUYANFORGE_COMPILER_BIN}")
    endif()
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME contract_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/validate_contracts.py)
        add_custom_target(check-contracts
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/validate_contracts.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
    endif()
endif()

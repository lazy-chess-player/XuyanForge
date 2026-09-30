add_library(xuyan_domain STATIC
    domain/src/hash.cpp
    domain/src/character_blueprint.cpp
    domain/src/character_instance.cpp
    domain/src/branch_outcome.cpp
    domain/src/evidence.cpp
    domain/src/extraction_candidate.cpp
    domain/src/extraction_job.cpp
    domain/src/scenario.cpp
    domain/src/simulation_session.cpp
    domain/src/source_document.cpp
    domain/src/provider_connection.cpp
    domain/src/retrieval.cpp
    domain/src/world_entity.cpp
    domain/src/world_version.cpp
    domain/src/world_graph.cpp
)
target_include_directories(xuyan_domain PUBLIC domain/include)
target_compile_features(xuyan_domain PUBLIC cxx_std_20)

add_library(xuyan_providers STATIC
    providers/src/model_protocol.cpp
    providers/src/sse_parser.cpp
)
target_include_directories(xuyan_providers PUBLIC providers/include)
target_link_libraries(xuyan_providers PUBLIC xuyan_domain xuyan_package)

add_library(xuyan_package STATIC
    package/src/json.cpp
    package/src/zip_archive.cpp
)
target_include_directories(xuyan_package PUBLIC package/include)
target_link_libraries(xuyan_package PUBLIC xuyan_domain)

add_library(xuyan_storage_sqlite STATIC
    storage_sqlite/src/workspace_repository.cpp
    storage_sqlite/src/sqlite_support.cpp
)
target_include_directories(xuyan_storage_sqlite PUBLIC storage_sqlite/include)
target_link_libraries(xuyan_storage_sqlite PUBLIC xuyan_domain SQLite::SQLite3 PRIVATE xuyan_package)

add_library(xuyan_application STATIC
    application/src/branch_outcome_service.cpp
    application/src/simulation_service.cpp
    application/src/backup_service.cpp
    application/src/evidence_service.cpp
    application/src/candidate_service.cpp
    application/src/extraction_output_contract.cpp
    application/src/extraction_job_service.cpp
    application/src/mock_extraction_processor.cpp
    application/src/remote_extraction_processor.cpp
    application/src/character_service.cpp
    application/src/character_instance_service.cpp
    application/src/package_service.cpp
    application/src/provider_connection_service.cpp
    application/src/provider_generation_service.cpp
    application/src/retrieval_service.cpp
    application/src/source_import_service.cpp
    application/src/source_text_decoder.cpp
    application/src/workspace_service.cpp
    application/src/world_catalog_service.cpp
    application/src/world_version_service.cpp
    application/src/world_graph_service.cpp
)
target_include_directories(xuyan_application PUBLIC application/include)
target_link_libraries(xuyan_application PUBLIC xuyan_domain xuyan_package xuyan_providers xuyan_storage_sqlite)

if(WIN32)
    add_library(xuyan_platform_credentials STATIC
        platform_native/src/credential_store_windows.cpp
    )
    target_include_directories(xuyan_platform_credentials PUBLIC platform_native/include)
    target_include_directories(xuyan_platform_credentials PRIVATE application/include domain/include)
    target_link_libraries(xuyan_platform_credentials PRIVATE advapi32)
else()
    add_library(xuyan_platform_credentials STATIC
        platform_native/src/credential_store_unavailable.cpp
    )
    target_include_directories(xuyan_platform_credentials PUBLIC platform_native/include)
    target_include_directories(xuyan_platform_credentials PRIVATE application/include domain/include)
endif()

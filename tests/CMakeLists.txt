# 固定响应与合成情景只参与测试，不进入正式应用的链接依赖。
add_library(xuyan_test_engine STATIC
    engine/mock_provider.cpp
    engine/simulation_engine.cpp
)
target_include_directories(xuyan_test_engine PUBLIC include)
target_link_libraries(xuyan_test_engine PUBLIC xuyan_domain)
if(XUYANFORGE_BUILD_UI)
    # 明确列出测试探针头，使 AUTOMOC 生成元对象；探针不参与正式程序链接。
    qt_add_executable(tst_qmltests
        main.cpp
        framework_option_probe.h
    )
    target_include_directories(tst_qmltests PRIVATE ${PROJECT_SOURCE_DIR}/ui/i18n)

    target_compile_definitions(tst_qmltests PRIVATE
        QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
    )

    target_link_libraries(tst_qmltests PRIVATE
        Qt6::Gui
        Qt6::Qml
        Qt6::QuickTest
    )

    add_test(NAME tst_qmltests COMMAND tst_qmltests)

    # 界面测试在无显示器的 CI 和本地终端中运行，不依赖用户桌面会话。
    set_tests_properties(tst_qmltests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    # 视图模型回归只链接 Qt Core 与应用层，不启动窗口或读取用户工作区。
    qt_add_executable(tst_world_views_vm
        world_views_vm_tests.cpp
        ${CMAKE_SOURCE_DIR}/ui/viewmodels/candidate_review_view_model.cpp
        ${CMAKE_SOURCE_DIR}/ui/viewmodels/candidate_review_view_model.h
        ${CMAKE_SOURCE_DIR}/ui/viewmodels/source_view_model.cpp
        ${CMAKE_SOURCE_DIR}/ui/viewmodels/source_view_model.h
        ${CMAKE_SOURCE_DIR}/ui/viewmodels/extraction_job_view_model.cpp
        ${CMAKE_SOURCE_DIR}/ui/viewmodels/extraction_job_view_model.h
        ${CMAKE_SOURCE_DIR}/ui/viewmodels/world_views_view_model.cpp
        ${CMAKE_SOURCE_DIR}/ui/viewmodels/world_views_view_model.h
        ${CMAKE_SOURCE_DIR}/ui/viewmodels/package_view_model.cpp
        ${CMAKE_SOURCE_DIR}/ui/viewmodels/package_view_model.h
    )
    target_include_directories(tst_world_views_vm PRIVATE ${CMAKE_SOURCE_DIR}/ui/viewmodels)
    target_link_libraries(tst_world_views_vm PRIVATE
        Qt6::Core Qt6::Network xuyan_application xuyan_platform_credentials xuyan_providers_qt SQLite::SQLite3)
    add_test(NAME tst_world_views_vm COMMAND tst_world_views_vm)
    if(WIN32)
        # 直接运行 CTest 也应使用构建所选 Qt/MinGW；否则 PATH 中其他 Qt 或运行库可能使界面测试卡住。
        set_tests_properties(tst_qmltests tst_world_views_vm PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${XUYANFORGE_COMPILER_BIN}"
        )
    endif()
endif()

add_executable(core_tests core_tests.cpp synthetic_world_data.cpp synthetic_fixture.cpp)
target_link_libraries(core_tests PRIVATE xuyan_application xuyan_test_engine xuyan_platform_credentials xuyan_providers)
add_test(NAME core_tests COMMAND core_tests)
add_executable(stress_tests stress_tests.cpp)
target_link_libraries(stress_tests PRIVATE xuyan_storage_sqlite)
add_test(NAME stress_tests COMMAND stress_tests)
add_executable(storage_support_tests storage_support_tests.cpp)
target_include_directories(storage_support_tests PRIVATE ${PROJECT_SOURCE_DIR}/libs/storage_sqlite/src)
target_link_libraries(storage_support_tests PRIVATE xuyan_storage_sqlite)
add_test(NAME storage_support_tests COMMAND storage_support_tests)
add_executable(domain_boundary_tests domain_boundary_tests.cpp)
target_link_libraries(domain_boundary_tests PRIVATE xuyan_domain)
add_test(NAME domain_boundary_tests COMMAND domain_boundary_tests)
add_executable(application_catalog_tests application_catalog_tests.cpp)
target_include_directories(application_catalog_tests PRIVATE ${PROJECT_SOURCE_DIR}/libs/storage_sqlite/src)
target_link_libraries(application_catalog_tests PRIVATE xuyan_application)
add_test(NAME application_catalog_tests COMMAND application_catalog_tests)
# 备份安全回归独立拥有临时资料；正式目标不链接任何测试素材或注入接口。
add_executable(backup_staging_tests backup_staging_tests.cpp)
target_include_directories(backup_staging_tests PRIVATE ${PROJECT_SOURCE_DIR}/libs/application/src)
add_test(NAME backup_staging_tests COMMAND backup_staging_tests)
add_executable(backup_safety_tests backup_safety_tests.cpp)
target_link_libraries(backup_safety_tests PRIVATE xuyan_application)
add_test(NAME backup_safety_tests COMMAND backup_safety_tests)
add_test(NAME backup_link_tests COMMAND backup_safety_tests --links-only)
add_executable(map_integrity_tests map_integrity_tests.cpp)

target_link_libraries(map_integrity_tests PRIVATE xuyan_application)
add_test(NAME map_integrity_tests COMMAND map_integrity_tests)
# 只允许独立链接分组报告权限/设施待条件；普通安全分组绝不因链接缺失而跳过。
set_tests_properties(backup_link_tests PROPERTIES SKIP_RETURN_CODE 77)
if(WIN32)
    set_tests_properties(core_tests PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${XUYANFORGE_COMPILER_BIN}")
    set_tests_properties(stress_tests PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${XUYANFORGE_COMPILER_BIN}")
    set_tests_properties(storage_support_tests PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${XUYANFORGE_COMPILER_BIN}")
    set_tests_properties(domain_boundary_tests PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${XUYANFORGE_COMPILER_BIN}")
    set_tests_properties(application_catalog_tests PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${XUYANFORGE_COMPILER_BIN}")
    set_tests_properties(backup_staging_tests PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${XUYANFORGE_COMPILER_BIN}")
    set_tests_properties(backup_safety_tests backup_link_tests PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${XUYANFORGE_COMPILER_BIN}")
    set_tests_properties(map_integrity_tests PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${XUYANFORGE_COMPILER_BIN}")
endif()
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME development_standards_tests
        COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${PROJECT_SOURCE_DIR}/tools -p test_development_standards.py -v)
    add_test(NAME development_standards
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_development_standards.py)
    add_test(NAME contract_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/validate_contracts.py)
    add_custom_target(check-contracts
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/validate_contracts.py
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()

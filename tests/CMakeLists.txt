qt_add_executable(tst_qmltests main.cpp)

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
)
target_include_directories(tst_world_views_vm PRIVATE ${CMAKE_SOURCE_DIR}/ui/viewmodels)
target_link_libraries(tst_world_views_vm PRIVATE
    Qt6::Core Qt6::Network xuyan_application xuyan_platform_credentials xuyan_providers_qt SQLite::SQLite3)
add_test(NAME tst_world_views_vm COMMAND tst_world_views_vm)

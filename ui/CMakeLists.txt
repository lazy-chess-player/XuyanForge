add_library(xuyan_providers_qt STATIC
    ${PROJECT_SOURCE_DIR}/libs/providers_qt/src/qt_provider_transport.cpp
)
target_include_directories(xuyan_providers_qt PUBLIC ${PROJECT_SOURCE_DIR}/libs/providers_qt/include)
target_link_libraries(xuyan_providers_qt PUBLIC xuyan_application Qt6::Core Qt6::Network)

qt_add_executable(xuyanforge_app
    ${PROJECT_SOURCE_DIR}/apps/native/main.cpp
    viewmodels/workspace_view_model.cpp
    viewmodels/workspace_view_model.h
    viewmodels/workspace_catalog_view_model.cpp
    viewmodels/workspace_catalog_view_model.h
    viewmodels/source_view_model.cpp
    viewmodels/source_view_model.h
    viewmodels/character_view_model.cpp
    viewmodels/character_view_model.h
    viewmodels/package_view_model.cpp
    viewmodels/package_view_model.h
    viewmodels/provider_view_model.cpp
    viewmodels/provider_view_model.h
    viewmodels/extraction_job_view_model.cpp
    viewmodels/extraction_job_view_model.h
    viewmodels/candidate_review_view_model.cpp
    viewmodels/candidate_review_view_model.h
    viewmodels/world_views_view_model.cpp
    viewmodels/world_views_view_model.h
)
target_include_directories(xuyanforge_app PRIVATE viewmodels)
target_compile_definitions(xuyanforge_app PRIVATE XUYANFORGE_VERSION="${XUYANFORGE_ARTIFACT_VERSION}")
target_link_libraries(xuyanforge_app PRIVATE
    xuyan_application xuyan_platform_credentials xuyan_providers_qt Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2
)

add_executable(xuyanforge_provider_cli ${PROJECT_SOURCE_DIR}/tools/provider_cli.cpp)
target_link_libraries(xuyanforge_provider_cli PRIVATE
    xuyan_application xuyan_platform_credentials xuyan_providers_qt Qt6::Core Qt6::Network
)
qt_add_qml_module(xuyanforge_app
    URI XuyanForge
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/UiTheme.qml
        qml/AppButton.qml
        qml/AppComboBox.qml
        qml/AppField.qml
        qml/SectionPanel.qml
        qml/ProjectSidebar.qml
        qml/HomePage.qml
        qml/SourcePage.qml
        qml/TaskPage.qml
        qml/ReviewPage.qml
        qml/EntityPage.qml
        qml/ProviderPage.qml
        qml/CharacterPage.qml
        qml/WorkspacePage.qml
        qml/PackagePage.qml
        qml/ViewsPage.qml
)

install(TARGETS xuyanforge_app
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

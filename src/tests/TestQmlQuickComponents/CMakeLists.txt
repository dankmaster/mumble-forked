find_pkg(Qt6 COMPONENTS Quick QuickControls2 QuickTest Qml REQUIRED)

add_executable(TestQmlQuickComponents
	TestQmlQuickComponents.cpp
	../../mumble/qml-shell.qrc
)

set_target_properties(TestQmlQuickComponents PROPERTIES AUTOMOC ON AUTORCC ON)
target_compile_definitions(TestQmlQuickComponents PRIVATE
	QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)
target_link_libraries(TestQmlQuickComponents PRIVATE
	Qt6::Core
	Qt6::Gui
	Qt6::Qml
	Qt6::Quick
	Qt6::QuickControls2
	Qt6::QuickTest
)

add_test(NAME TestQmlQuickComponents COMMAND $<TARGET_FILE:TestQmlQuickComponents>)
configure_client_test_runtime(TestQmlQuickComponents TestQmlQuickComponents)
set_tests_properties(
	TestQmlQuickComponents
	PROPERTIES
		ENVIRONMENT
			"QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>;QML2_IMPORT_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/../../qml;QML_IMPORT_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/../../qml;QSG_RENDER_LOOP=basic;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic"
		ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

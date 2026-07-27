add_executable(TestQmlAccessibilitySnapshot
	TestQmlAccessibilitySnapshot.cpp
	../../mumble/QmlAccessibilitySnapshot.cpp
)

set_target_properties(TestQmlAccessibilitySnapshot PROPERTIES AUTOMOC ON)
find_pkg(Qt6 COMPONENTS Gui REQUIRED)
target_include_directories(TestQmlAccessibilitySnapshot PRIVATE ../../mumble)
target_link_libraries(TestQmlAccessibilitySnapshot PRIVATE Qt6::Core Qt6::Gui Qt6::Test)

add_test(NAME TestQmlAccessibilitySnapshot COMMAND $<TARGET_FILE:TestQmlAccessibilitySnapshot>)
configure_client_test_runtime(TestQmlAccessibilitySnapshot TestQmlAccessibilitySnapshot)
set_tests_properties(
	TestQmlAccessibilitySnapshot
	PROPERTIES
		ENVIRONMENT
			"QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..;QML2_IMPORT_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/../../qml;QML_IMPORT_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/../../qml;QSG_RENDER_LOOP=basic;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
		ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

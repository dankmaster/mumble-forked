add_executable(TestScreenShareVideoItem
	TestScreenShareVideoItem.cpp
	../../mumble/ScreenShareVideoItem.cpp
	../../mumble/ScreenShareViewBackend.cpp
)
set_target_properties(TestScreenShareVideoItem PROPERTIES AUTOMOC ON)
find_pkg(Qt6 COMPONENTS Gui Quick REQUIRED)
target_include_directories(TestScreenShareVideoItem PRIVATE ../../mumble)
target_link_libraries(TestScreenShareVideoItem PRIVATE shared Qt6::Gui Qt6::Quick Qt6::Test)
add_test(NAME TestScreenShareVideoItem COMMAND $<TARGET_FILE:TestScreenShareVideoItem>)
configure_client_test_runtime(TestScreenShareVideoItem TestScreenShareVideoItem)
set_tests_properties(
	TestScreenShareVideoItem
	PROPERTIES
		ENVIRONMENT
			"QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>;QSG_RENDER_LOOP=basic;QT_QUICK_BACKEND=software"
		ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

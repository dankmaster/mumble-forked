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
set_property(
	TEST
	TestScreenShareVideoItem
	APPEND PROPERTY ENVIRONMENT "QSG_RENDER_LOOP=basic;QT_QUICK_BACKEND=software"
)

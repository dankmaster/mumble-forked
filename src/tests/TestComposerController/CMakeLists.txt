add_executable(
	TestComposerController
	TestComposerController.cpp
	../../mumble/ChatAttachment.cpp
	../../mumble/ComposerController.cpp
	../../mumble/QmlImageProvider.cpp
)
set_target_properties(TestComposerController PROPERTIES AUTOMOC ON)
find_pkg(Qt6 COMPONENTS Concurrent Gui Quick REQUIRED)
target_include_directories(TestComposerController PRIVATE ../../mumble)
target_link_libraries(TestComposerController PRIVATE Qt6::Concurrent Qt6::Gui Qt6::Quick Qt6::Test)
add_test(NAME TestComposerController COMMAND $<TARGET_FILE:TestComposerController>)
configure_client_test_runtime(TestComposerController TestComposerController)

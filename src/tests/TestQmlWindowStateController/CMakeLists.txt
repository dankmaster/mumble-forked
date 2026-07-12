add_executable(TestQmlWindowStateController
	TestQmlWindowStateController.cpp
	../../mumble/QmlWindowStateController.cpp
)

set_target_properties(TestQmlWindowStateController PROPERTIES AUTOMOC ON)
find_pkg(Qt6 COMPONENTS Gui REQUIRED)
target_include_directories(TestQmlWindowStateController PRIVATE ../../mumble)
target_link_libraries(TestQmlWindowStateController PRIVATE Qt6::Core Qt6::Gui Qt6::Test)

add_test(NAME TestQmlWindowStateController COMMAND $<TARGET_FILE:TestQmlWindowStateController>)
configure_client_test_runtime(TestQmlWindowStateController TestQmlWindowStateController)

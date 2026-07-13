add_executable(TestQmlMediaProfileFactory
	TestQmlMediaProfileFactory.cpp
	../../mumble/ClientActionRegistry.cpp
	../../mumble/QmlClientModels.cpp
	../../mumble/QmlMediaProfileFactory.cpp
)

set_target_properties(TestQmlMediaProfileFactory PROPERTIES AUTOMOC ON)

find_pkg(Qt6 COMPONENTS Concurrent Gui Network WebEngineQuick REQUIRED)

target_include_directories(TestQmlMediaProfileFactory PRIVATE ../../mumble)
target_link_libraries(TestQmlMediaProfileFactory PRIVATE
	Qt6::Concurrent
	Qt6::Core
	Qt6::Gui
	Qt6::Network
	Qt6::Test
	Qt6::WebEngineQuick
)

add_test(NAME TestQmlMediaProfileFactory COMMAND $<TARGET_FILE:TestQmlMediaProfileFactory>)
configure_client_test_runtime(TestQmlMediaProfileFactory TestQmlMediaProfileFactory)

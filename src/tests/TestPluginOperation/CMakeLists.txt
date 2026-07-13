add_executable(TestPluginOperation
	TestPluginOperation.cpp
	../../mumble/PluginOperation.cpp
)
set_target_properties(TestPluginOperation PROPERTIES AUTOMOC ON)
find_pkg(Qt6 COMPONENTS Concurrent Test REQUIRED)
target_include_directories(TestPluginOperation PRIVATE ../../mumble)
target_link_libraries(TestPluginOperation PRIVATE Qt6::Concurrent Qt6::Core Qt6::Test)
add_test(NAME TestPluginOperation COMMAND $<TARGET_FILE:TestPluginOperation>)
configure_client_test_runtime(TestPluginOperation TestPluginOperation)

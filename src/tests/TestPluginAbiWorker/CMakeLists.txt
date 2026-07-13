add_executable(TestPluginAbiWorker
	TestPluginAbiWorker.cpp
	../../mumble/PluginAbiWorker.cpp
)
set_target_properties(TestPluginAbiWorker PROPERTIES AUTOMOC ON)
find_pkg(Qt6 COMPONENTS Concurrent Test REQUIRED)
target_include_directories(TestPluginAbiWorker PRIVATE ../../mumble)
target_compile_definitions(TestPluginAbiWorker PRIVATE
	PLUGIN_MANAGER_SOURCE_PATH="${CMAKE_SOURCE_DIR}/src/mumble/PluginManager.cpp"
)
target_link_libraries(TestPluginAbiWorker PRIVATE Qt6::Concurrent Qt6::Core Qt6::Test)
add_test(NAME TestPluginAbiWorker COMMAND $<TARGET_FILE:TestPluginAbiWorker>)
configure_client_test_runtime(TestPluginAbiWorker TestPluginAbiWorker)

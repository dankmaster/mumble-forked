add_executable(TestPluginUpdatePreparation
	TestPluginUpdatePreparation.cpp
	../../mumble/PluginManifest.cpp
	../../mumble/PluginUpdatePreparation.cpp
)
set_target_properties(TestPluginUpdatePreparation PROPERTIES AUTOMOC ON)
find_pkg(Qt6 COMPONENTS Test REQUIRED)
find_pkg(Poco COMPONENTS XML REQUIRED)
target_include_directories(TestPluginUpdatePreparation PRIVATE ../../mumble)
target_link_libraries(TestPluginUpdatePreparation PRIVATE Poco::XML Qt6::Core Qt6::Test)
add_test(NAME TestPluginUpdatePreparation COMMAND $<TARGET_FILE:TestPluginUpdatePreparation>)
configure_client_test_runtime(TestPluginUpdatePreparation TestPluginUpdatePreparation)

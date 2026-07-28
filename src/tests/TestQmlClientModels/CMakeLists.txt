add_executable(TestQmlClientModels
	TestQmlClientModels.cpp
	../../mumble/ClientActionRegistry.cpp
	../../mumble/EmbedDocument.cpp
	../../mumble/QmlClientModels.cpp
)

set_target_properties(TestQmlClientModels PROPERTIES AUTOMOC ON)

find_pkg(Qt6 COMPONENTS Concurrent Gui REQUIRED)

target_include_directories(TestQmlClientModels PRIVATE ../../mumble)
target_link_libraries(TestQmlClientModels PRIVATE Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Test)

add_test(NAME TestQmlClientModels COMMAND $<TARGET_FILE:TestQmlClientModels>)

configure_client_test_runtime(TestQmlClientModels TestQmlClientModels)

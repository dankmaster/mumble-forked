add_executable(TestQmlClientModels
	TestQmlClientModels.cpp
)

set_target_properties(TestQmlClientModels PROPERTIES AUTOMOC ON)

target_link_libraries(TestQmlClientModels PRIVATE mumble_client_object_lib shared Qt6::Test)

add_test(NAME TestQmlClientModels COMMAND $<TARGET_FILE:TestQmlClientModels>)

configure_client_test_runtime(TestQmlClientModels TestQmlClientModels)

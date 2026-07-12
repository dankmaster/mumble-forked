add_executable(TestQmlPerformanceMonitor
	TestQmlPerformanceMonitor.cpp
	../../mumble/QmlPerformanceMonitor.cpp
)

set_target_properties(TestQmlPerformanceMonitor PROPERTIES AUTOMOC ON)
find_pkg(Qt6 COMPONENTS Test REQUIRED)
target_include_directories(TestQmlPerformanceMonitor PRIVATE ../../mumble)
target_link_libraries(TestQmlPerformanceMonitor PRIVATE Qt6::Core Qt6::Test)
add_test(NAME TestQmlPerformanceMonitor COMMAND $<TARGET_FILE:TestQmlPerformanceMonitor>)
configure_client_test_runtime(TestQmlPerformanceMonitor TestQmlPerformanceMonitor)

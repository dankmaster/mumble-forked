add_executable(TestQmlMediaProfileFactory
	TestQmlMediaProfileFactory.cpp
	../../mumble/adaptive-media-player.qrc
	../../mumble/ClientActionRegistry.cpp
	../../mumble/QmlClientModels.cpp
	../../mumble/QmlMediaProfileFactory.cpp
)

set_target_properties(TestQmlMediaProfileFactory PROPERTIES AUTOMOC ON AUTORCC ON)

find_pkg(Qt6 COMPONENTS Concurrent Gui Network Qml Quick WebChannelQuick WebEngineQuick REQUIRED)

target_include_directories(TestQmlMediaProfileFactory PRIVATE ../../mumble)
target_link_libraries(TestQmlMediaProfileFactory PRIVATE
	Qt6::Concurrent
	Qt6::Core
	Qt6::Gui
	Qt6::Network
	Qt6::Qml
	Qt6::Quick
	Qt6::Test
	Qt6::WebEngineQuick
)

if(MSVC AND NOT static)
	# Match the product executable's Windows media boundary so this test can
	# assert both sides of the contract in-process: no module at startup, then a
	# usable profile and QML WebEngineView after the explicit media action.
	target_link_options(TestQmlMediaProfileFactory PRIVATE
		"/DELAYLOAD:$<TARGET_FILE_NAME:Qt6::WebEngineQuick>"
		"/DELAYLOAD:$<TARGET_FILE_NAME:Qt6::WebEngineCore>"
	)
	target_link_libraries(TestQmlMediaProfileFactory PRIVATE Delayimp.lib)
	target_compile_definitions(TestQmlMediaProfileFactory PRIVATE
		MUMBLE_TEST_DELAYED_WEBENGINE=1
		MUMBLE_TEST_WEBENGINE_QUICK_DLL="$<TARGET_FILE_NAME:Qt6::WebEngineQuick>"
		MUMBLE_TEST_WEBENGINE_CORE_DLL="$<TARGET_FILE_NAME:Qt6::WebEngineCore>"
		MUMBLE_TEST_WEBCHANNEL_QUICK_DLL="$<TARGET_FILE_NAME:Qt6::WebChannelQuick>"
	)
endif()

add_executable(TestShakaLicense
	TestShakaLicense.cpp
	../../License.cpp
)

set_target_properties(TestShakaLicense PROPERTIES AUTOMOC ON)
target_include_directories(TestShakaLicense PRIVATE ../.. ${CMAKE_BINARY_DIR}/src)
target_link_libraries(TestShakaLicense PRIVATE Qt6::Core Qt6::Test)
add_dependencies(TestShakaLicense generate_license_header)
add_test(NAME TestShakaLicense COMMAND $<TARGET_FILE:TestShakaLicense>)
configure_client_test_runtime(TestShakaLicense TestShakaLicense)

add_test(NAME TestQmlMediaProfileFactory COMMAND $<TARGET_FILE:TestQmlMediaProfileFactory>)
configure_client_test_runtime(TestQmlMediaProfileFactory TestQmlMediaProfileFactory)
set_property(TEST TestQmlMediaProfileFactory APPEND PROPERTY ENVIRONMENT
	"QSG_RENDER_LOOP=basic"
	"QT_QUICK_BACKEND=software"
)

if(WIN32 AND TARGET Qt6::qtwebenginequickplugin)
	# The test executes from the build tree rather than a windeployqt stage. Point
	# the first real profile/view activation at Qt's development-tree runtime;
	# none of these paths load WebEngine before the tested profile request.
	set_property(TEST TestQmlMediaProfileFactory APPEND PROPERTY ENVIRONMENT
		"QML_IMPORT_PATH=$<TARGET_FILE_DIR:Qt6::qtwebenginequickplugin>/.."
		"QML2_IMPORT_PATH=$<TARGET_FILE_DIR:Qt6::qtwebenginequickplugin>/.."
		"QTWEBENGINEPROCESS_PATH=${QT6_INSTALL_PREFIX}/tools/Qt6/bin/QtWebEngineProcess.exe"
		"QTWEBENGINE_RESOURCES_PATH=$<TARGET_FILE_DIR:Qt6::WebEngineCore>/../resources"
		"QTWEBENGINE_LOCALES_PATH=${QT6_INSTALL_PREFIX}/translations/Qt6/qtwebengine_locales"
	)
endif()

add_executable(
	TestChatAttachmentUploader
	TestChatAttachmentUploader.cpp
	../../mumble/ChatAttachmentUploader.cpp
)
set_target_properties(TestChatAttachmentUploader PROPERTIES AUTOMOC ON)
target_include_directories(TestChatAttachmentUploader PRIVATE ../../mumble)
target_link_libraries(TestChatAttachmentUploader PRIVATE shared Qt6::Test)
add_test(NAME TestChatAttachmentUploader COMMAND $<TARGET_FILE:TestChatAttachmentUploader>)
configure_client_test_runtime(TestChatAttachmentUploader TestChatAttachmentUploader)

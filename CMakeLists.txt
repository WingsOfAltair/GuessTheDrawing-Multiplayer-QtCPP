cmake_minimum_required(VERSION 3.21)
project(GuessTheDrawing VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Multimedia Network Sql Widgets)

add_library(gtd_shared
	src/shared/app_config.cpp
	src/shared/app_config.h
	src/shared/auth_token.cpp
	src/shared/auth_token.h
	src/shared/models.cpp
	src/shared/protocol.cpp
	src/shared/protocol.h
	src/shared/models.h
	src/shared/game_rules.cpp
	src/shared/game_rules.h
)
target_include_directories(gtd_shared PUBLIC src)
target_link_libraries(gtd_shared PUBLIC Qt6::Core Qt6::Network)

add_library(gtd_storage
	src/storage/account_store.cpp
	src/storage/account_store.h
)
target_include_directories(gtd_storage PUBLIC src)
target_link_libraries(gtd_storage PUBLIC gtd_shared Qt6::Sql Qt6::Network)

add_executable(gtd_master src/master/main.cpp src/master/master_server.cpp src/master/master_server.h)
target_link_libraries(gtd_master PRIVATE gtd_storage gtd_shared Qt6::Core Qt6::Network Qt6::Sql)

add_executable(gtd_game_server src/game/main.cpp src/game/game_server.cpp src/game/game_server.h)
target_link_libraries(gtd_game_server PRIVATE gtd_shared Qt6::Core Qt6::Network)

add_executable(gtd_client src/client/main.cpp src/client/main_window.cpp src/client/main_window.h src/client/drawing_canvas.cpp src/client/drawing_canvas.h)
foreach(dictionary_target IN ITEMS gtd_game_server gtd_client)
	add_custom_command(TARGET ${dictionary_target} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_SOURCE_DIR}/dictionary"
			"$<TARGET_FILE_DIR:${dictionary_target}>/dictionary"
		VERBATIM
	)
endforeach()
foreach(profanity_target IN ITEMS gtd_game_server gtd_master)
	add_custom_command(TARGET ${profanity_target} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_SOURCE_DIR}/profanity-dictionary.txt"
			"$<TARGET_FILE_DIR:${profanity_target}>/profanity-dictionary.txt"
		VERBATIM
	)
endforeach()
file(GLOB COUNTRY_FLAG_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/country-flags/*.png")
qt_add_resources(gtd_client country_flags
	PREFIX "/country-flags"
	BASE "${CMAKE_CURRENT_SOURCE_DIR}/country-flags"
	FILES ${COUNTRY_FLAG_FILES}
)
if(WIN32)
	set_target_properties(gtd_client PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
target_link_libraries(gtd_client PRIVATE gtd_shared Qt6::Core Qt6::Multimedia Qt6::Network Qt6::Widgets)

include(CTest)
if(BUILD_TESTING)
	find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
	add_executable(gtd_account_tests tests/test_account_store.cpp)
	target_link_libraries(gtd_account_tests PRIVATE gtd_storage Qt6::Test)
	add_test(NAME gtd_account_tests COMMAND gtd_account_tests)
	add_executable(gtd_rules_tests tests/test_game_rules.cpp)
	target_link_libraries(gtd_rules_tests PRIVATE gtd_shared Qt6::Test)
	add_test(NAME gtd_rules_tests COMMAND gtd_rules_tests)
	add_executable(gtd_protocol_tests tests/test_protocol.cpp)
	target_link_libraries(gtd_protocol_tests PRIVATE gtd_shared Qt6::Test)
	add_test(NAME gtd_protocol_tests COMMAND gtd_protocol_tests)
	add_executable(gtd_canvas_tests tests/test_canvas_models.cpp)
	target_link_libraries(gtd_canvas_tests PRIVATE gtd_shared Qt6::Test)
	add_test(NAME gtd_canvas_tests COMMAND gtd_canvas_tests)
	add_executable(gtd_drawing_canvas_tests tests/test_drawing_canvas.cpp src/client/drawing_canvas.cpp src/client/drawing_canvas.h)
	target_link_libraries(gtd_drawing_canvas_tests PRIVATE gtd_shared Qt6::Widgets Qt6::Test)
	add_test(NAME gtd_drawing_canvas_tests COMMAND gtd_drawing_canvas_tests)
	add_executable(gtd_game_server_tests tests/test_game_server.cpp src/game/game_server.cpp src/game/game_server.h)
	target_link_libraries(gtd_game_server_tests PRIVATE gtd_shared Qt6::Test)
	add_custom_command(TARGET gtd_game_server_tests POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_SOURCE_DIR}/profanity-dictionary.txt"
			"$<TARGET_FILE_DIR:gtd_game_server_tests>/profanity-dictionary.txt"
		VERBATIM
	)
	add_custom_command(TARGET gtd_game_server_tests POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_SOURCE_DIR}/dictionary"
			"$<TARGET_FILE_DIR:gtd_game_server_tests>/dictionary"
		VERBATIM
	)
	add_test(NAME gtd_game_server_tests COMMAND gtd_game_server_tests)
	add_executable(gtd_master_tests tests/test_master_server.cpp src/master/master_server.cpp src/master/master_server.h)
	target_link_libraries(gtd_master_tests PRIVATE gtd_storage gtd_shared Qt6::Test)
	add_custom_command(TARGET gtd_master_tests POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_SOURCE_DIR}/profanity-dictionary.txt"
			"$<TARGET_FILE_DIR:gtd_master_tests>/profanity-dictionary.txt"
		VERBATIM
	)
	add_test(NAME gtd_master_tests COMMAND gtd_master_tests)
	add_executable(gtd_app_config_tests tests/test_app_config.cpp)
	target_link_libraries(gtd_app_config_tests PRIVATE gtd_shared Qt6::Test)
	add_test(NAME gtd_app_config_tests COMMAND gtd_app_config_tests)
endif()

include(FetchContent)

set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS ON CACHE BOOL "" FORCE)
set(CATCH_DEVELOPMENT_BUILD OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

include(Catch)

add_executable(trader_ocr_test RoguelikeTraderOcrTest.cpp)

if(MSVC)
    target_compile_options(trader_ocr_test PRIVATE "/WX-")
else()
    target_compile_options(trader_ocr_test PRIVATE "-Wno-error")
endif()

target_link_libraries(trader_ocr_test PRIVATE MaaCore Catch2::Catch2WithMain)
target_include_directories(trader_ocr_test PRIVATE ${PROJECT_SOURCE_DIR}/src/MaaCore)
add_dependencies(trader_ocr_test MaaCore)
create_resource_link(trader_ocr_test "$<TARGET_FILE_DIR:trader_ocr_test>")

catch_discover_tests(trader_ocr_test TEST_PREFIX "vision::")

add_executable(trader_ocr_cli main.cpp)
target_link_libraries(trader_ocr_cli PRIVATE MaaCore)
target_include_directories(trader_ocr_cli PRIVATE ${PROJECT_SOURCE_DIR}/src/MaaCore)
add_dependencies(trader_ocr_cli MaaCore)
create_resource_link(trader_ocr_cli "$<TARGET_FILE_DIR:trader_ocr_cli>")

add_executable(collectible_select_ocr_cli collectible_select_ocr_cli.cpp)
target_link_libraries(collectible_select_ocr_cli PRIVATE MaaCore)
target_include_directories(collectible_select_ocr_cli PRIVATE ${PROJECT_SOURCE_DIR}/src/MaaCore)
add_dependencies(collectible_select_ocr_cli MaaCore)
create_resource_link(collectible_select_ocr_cli "$<TARGET_FILE_DIR:collectible_select_ocr_cli>")

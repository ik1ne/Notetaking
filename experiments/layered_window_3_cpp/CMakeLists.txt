cmake_minimum_required(VERSION 3.15)
project(WebView2App3)

set(CMAKE_CXX_STANDARD 17)

include_directories("../microsoft.web.webview2.1.0.3179.45/build/native/include")
link_directories("../microsoft.web.webview2.1.0.3179.45/build/native/x64")

add_executable(WebView2App3 main.cpp)
target_link_libraries(WebView2App3 WebView2Loader ole32 user32)
target_link_options(WebView2App3 PRIVATE /SUBSYSTEM:WINDOWS)
add_custom_command(TARGET WebView2App3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/../microsoft.web.webview2.1.0.3179.45/build/native/x64/WebView2Loader.dll"
        $<TARGET_FILE_DIR:WebView2App3>)

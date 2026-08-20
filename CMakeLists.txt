cmake_minimum_required(VERSION 3.21)

project(jetson_control_gui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

add_library(jetson_control_core STATIC
    src/connection_config.cpp
    src/connection_config.h
    src/console_output_classifier.cpp
    src/console_output_classifier.h
    src/diagnostic_command_builder.cpp
    src/diagnostic_command_builder.h
    src/remote_command_runner.cpp
    src/remote_command_runner.h
    src/ssh_command_builder.cpp
    src/ssh_command_builder.h
)
target_link_libraries(jetson_control_core PUBLIC Qt6::Core)
target_include_directories(jetson_control_core PUBLIC src)

add_executable(jetson-control
    src/main.cpp
    src/main_window.cpp
    src/main_window.h
)
target_link_libraries(jetson-control PRIVATE
    jetson_control_core
    Qt6::Widgets
)

if(MSVC)
    target_compile_options(jetson_control_core PRIVATE /W4 /permissive-)
    target_compile_options(jetson-control PRIVATE /W4 /permissive-)
else()
    target_compile_options(jetson_control_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(jetson-control PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

    add_executable(test-ssh-command-builder
        tests/test_ssh_command_builder.cpp
    )
    target_link_libraries(test-ssh-command-builder PRIVATE
        jetson_control_core
        Qt6::Test
    )
    add_test(NAME ssh-command-builder COMMAND test-ssh-command-builder)

    add_executable(test-connection-config
        tests/test_connection_config.cpp
    )
    target_link_libraries(test-connection-config PRIVATE
        jetson_control_core
        Qt6::Test
    )
    add_test(NAME connection-config COMMAND test-connection-config)

    add_executable(test-diagnostic-command-builder
        tests/test_diagnostic_command_builder.cpp
    )
    target_link_libraries(test-diagnostic-command-builder PRIVATE
        jetson_control_core
        Qt6::Test
    )
    add_test(NAME diagnostic-command-builder COMMAND test-diagnostic-command-builder)

    add_executable(test-console-output-classifier
        tests/test_console_output_classifier.cpp
    )
    target_link_libraries(test-console-output-classifier PRIVATE
        jetson_control_core
        Qt6::Test
    )
    add_test(NAME console-output-classifier COMMAND test-console-output-classifier)
endif()

install(TARGETS jetson-control RUNTIME DESTINATION bin)

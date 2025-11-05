cmake_minimum_required(VERSION 3.24)
project(lsm_examples LANGUAGES CXX)

add_library(example_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  target_compile_options(example_warnings INTERFACE /W4 /WX /permissive-)
elseif(MSVC)
  target_compile_options(example_warnings INTERFACE /W4 /WX)
else()
  target_compile_options(example_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()

function(add_example name src)
  add_executable(${name} ${src})
  target_link_libraries(${name} PRIVATE lsm::lsm example_warnings)
endfunction()

add_example(example_door door.cpp)
add_example(example_traffic_light traffic_light_priorities.cpp)
add_example(example_processing_queue processing_queue.cpp)
add_example(example_deferral deferral.cpp)
add_example(example_deferral_gate deferral_gate.cpp)
add_example(example_turnstile turnstile.cpp)
add_example(example_completion completion.cpp)
add_example(example_completion_splitter completion_splitter.cpp)
add_example(example_coroutine_cancellation coroutine_cancellation.cpp)
add_example(example_coroutine_retry coroutine_retry.cpp)
add_example(example_coroutine_timeout coroutine_timeout.cpp)
add_example(example_unhandled_hooks unhandled_hooks.cpp)
add_example(example_coroutine_publisher coroutine_publisher.cpp)
add_example(example_publisher_queue publisher_queue.cpp)
add_example(example_handlers_return_output handlers_return_output.cpp)
add_example(example_handlers_publisher handlers_publisher.cpp)

add_custom_target(examples
  DEPENDS
    example_door
    example_traffic_light
    example_processing_queue
    example_deferral
    example_deferral_gate
    example_turnstile
    example_completion
    example_completion_splitter
    example_coroutine_cancellation
    example_coroutine_retry
    example_coroutine_timeout
    example_coroutine_publisher
    example_unhandled_hooks
    example_publisher_queue
    example_handlers_return_output
    example_handlers_publisher
)

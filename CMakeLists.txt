cmake_minimum_required(VERSION 3.10)

project(checker CXX)

set (CMAKE_CXX_STANDARD 17)
if (NOT MSVC)
  set(CMAKE_CXX_FLAGS "-Wall -Wextra -pedantic")
endif()

add_executable(checker checker.cc)

if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set_target_properties(checker PROPERTIES
        OSX_ARCHITECTURES "x86_64;arm64"
    )
endif()

enable_testing()

# checker reads domains.txt from its working directory. The .invalid TLD is
# reserved (RFC 2606) and never resolves, so this test needs no network access
# and checker must exit non-zero.
add_test(NAME unresolvable_domain_fails COMMAND checker
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests/unresolvable)
set_tests_properties(unresolvable_domain_fails PROPERTIES WILL_FAIL TRUE)

# A missing domains.txt must also be reported as a failure.
add_test(NAME missing_domains_file_fails COMMAND checker
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
set_tests_properties(missing_domains_file_fails PROPERTIES WILL_FAIL TRUE)

# 10.255.255.1 is a private address that normally doesn't answer, so connect()
# hangs until the connect timeout gives up. TIMEOUT makes CTest kill checker
# (and fail the test) if that timeout ever stops working.
add_test(NAME unroutable_address_times_out COMMAND checker
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests/unroutable)
set_tests_properties(unroutable_address_times_out PROPERTIES
                     WILL_FAIL TRUE TIMEOUT 15)

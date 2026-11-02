cmake_minimum_required(VERSION 3.10)

project(checker CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

# checker uses POSIX sockets, so stop early with a clear message on Windows
if (WIN32)
  message(FATAL_ERROR "checker uses POSIX sockets and does not build on Windows")
endif()

add_executable(checker checker.cc)
target_compile_options(checker PRIVATE -Wall -Wextra -pedantic)

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

# CRLF line endings, indentation, blank lines and comments around localhost.
# localhost always resolves, so only the DNS output is checked; nothing listens
# on port 443, so the exit code is ignored (PASS_REGULAR_EXPRESSION does that).
add_test(NAME line_handling COMMAND checker
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests/line_handling)
set_tests_properties(line_handling PROPERTIES
                     PASS_REGULAR_EXPRESSION "DNS lookup successful"
                     FAIL_REGULAR_EXPRESSION "DNS lookup failed")

# A file with only comments and blank lines checks nothing and succeeds.
add_test(NAME no_domains_succeeds COMMAND checker
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests/no_domains)

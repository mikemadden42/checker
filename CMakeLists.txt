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

set(TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests)

# The .invalid TLD is reserved (RFC 2606) and never resolves, so this test
# needs no network access and checker must exit non-zero.
add_test(NAME unresolvable_domain_fails
         COMMAND checker ${TESTS_DIR}/unresolvable/domains.txt)
set_tests_properties(unresolvable_domain_fails PROPERTIES WILL_FAIL TRUE)

# A missing domains file must also be reported as a failure.
add_test(NAME missing_domains_file_fails
         COMMAND checker ${TESTS_DIR}/does-not-exist.txt)
set_tests_properties(missing_domains_file_fails PROPERTIES WILL_FAIL TRUE)

# 10.255.255.1 is a private address that normally doesn't answer, so connect()
# hangs until the connect timeout gives up. TIMEOUT makes CTest kill checker
# (and fail the test) if that timeout ever stops working.
add_test(NAME unroutable_address_times_out
         COMMAND checker ${TESTS_DIR}/unroutable/domains.txt)
set_tests_properties(unroutable_address_times_out PROPERTIES
                     WILL_FAIL TRUE TIMEOUT 15)

# CRLF line endings, indentation, blank lines and comments around localhost.
# localhost always resolves, so only the DNS output is checked; nothing listens
# on port 443, so the exit code is ignored (PASS_REGULAR_EXPRESSION does that).
add_test(NAME line_handling
         COMMAND checker ${TESTS_DIR}/line_handling/domains.txt)
set_tests_properties(line_handling PROPERTIES
                     PASS_REGULAR_EXPRESSION "DNS lookup successful"
                     FAIL_REGULAR_EXPRESSION "DNS lookup failed")

# With no FILE argument, checker reads domains.txt from its working directory.
# This file has only comments and blank lines, so nothing is checked and it
# succeeds.
add_test(NAME no_domains_succeeds COMMAND checker
         WORKING_DIRECTORY ${TESTS_DIR}/no_domains)

# -p changes the port that is tested.
add_test(NAME port_option
         COMMAND checker -p 1 ${TESTS_DIR}/line_handling/domains.txt)
set_tests_properties(port_option PROPERTIES
                     PASS_REGULAR_EXPRESSION "port 1 ")

# Bad ports, unknown options and extra arguments are usage errors.
add_test(NAME invalid_port_rejected COMMAND checker -p 70000)
set_tests_properties(invalid_port_rejected PROPERTIES
                     PASS_REGULAR_EXPRESSION "Invalid port: 70000")
add_test(NAME non_numeric_port_rejected COMMAND checker -p https)
set_tests_properties(non_numeric_port_rejected PROPERTIES
                     PASS_REGULAR_EXPRESSION "Invalid port: https")
add_test(NAME unknown_option_rejected COMMAND checker -x)
set_tests_properties(unknown_option_rejected PROPERTIES
                     PASS_REGULAR_EXPRESSION "Usage:")
add_test(NAME too_many_arguments_rejected COMMAND checker a.txt b.txt)
set_tests_properties(too_many_arguments_rejected PROPERTIES
                     PASS_REGULAR_EXPRESSION "Too many arguments")

cmake_minimum_required(VERSION 2.8.11)

set(TEST_DATA_PATH ${CMAKE_SOURCE_DIR}/search/tests/data/)
set(OFFSET_DATA_PATH ${TEST_DATA_PATH}/subindex_offsets/)

add_test(and_tt WORKING_DIRECTORY ${OFFSET_DATA_PATH})
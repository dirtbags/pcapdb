cmake_minimum_required(VERSION 2.8.11)
set(TEST_PCAP ${CMAKE_SOURCE_DIR}/tests/data/test.pcap)
set(TEST_WORKING_DIR ${CMAKE_SOURCE_DIR}/..)
MESSAGE("${TEST_PCAP}")

set(CMAKE_BUILD_TYPE Debug)

#add_definitions(-DDEBUG_ON)
add_definitions(-DPRINT_LOGS)

add_executable(enum_pcap net_proto.c net_proto.h)
target_link_libraries(enum_pcap pcap)

add_executable(fcap_order
    fcap_order.c
    ../bucketize.c
    ../keys.c
    ../event.c
    ../network.c
    ../pcapdb_init.c
    ../queue.c)
target_link_libraries(fcap_order pcap pfring pthread hugetlbfs numa)

set(QUEUE_T_SRC queue_test.c ../queue.c ../queue.h)
set(QUEUE_T_LIB pthread)
add_executable(queue_test ${QUEUE_T_SRC})
target_link_libraries(queue_test ${QUEUE_T_LIB})
add_test(queue_tt queue_test)

set(EVENT_T_SRC event_test.c ../event.c ../event.h)
set(EVENT_T_LIB pthread)
add_executable(event_test ${EVENT_T_SRC})
target_link_libraries(event_test ${EVENT_T_LIB})
add_test(event_tt event_test)

set(BASE_SRC
    ../pcapdb_init.c
    ../db.c
    ../bucketize.c
    ../network.c
    ../event.c
    ../queue.c
    ../keys.c
)
set(BASE_LIBS
    pfring
    pthread
    hugetlbfs
    numa
    pcap
    pq)
set(MEM_T_SRC
    mem_test.c
    test_args.c
    ${BASE_SRC}
)
set(MEM_T_LIB ${BASE_LIBS})
add_executable(mem_test ${MEM_T_SRC})
target_link_libraries(mem_test ${MEM_T_LIB})
add_test(mem_tt mem_test -t -C ${TEST_WORKING_DIR})

set(BUCK_T_SRC
    bucketize_test.c
    ../capture.c
    test_args.c
    ${BASE_SRC}
)
add_executable(bucketize_test ${BUCK_T_SRC})
target_link_libraries(bucketize_test ${MEM_T_LIB})
add_test(bucketize_tt bucketize_test -t ${TEST_PCAP})

set(INDEX_T_SRC
    index_test.c
    ../index.c
    ../capture.c
    test_args.c
    ${BASE_SRC}
)
add_executable(index_test ${INDEX_T_SRC})
target_link_libraries(index_test ${MEM_T_LIB})
add_test(index_tt index_test -t ${TEST_PCAP})

add_executable(ipcmp_test ipcmp_test.c
    ../keys.c
    ../network.c
    ../network.h)
target_link_libraries(ipcmp_test ${BASE_LIBS})
add_test(ipcmp_tt ipcmp_test ${CMAKE_SOURCE_DIR}/tests/test_ips)

add_executable(ip6cmp_test ip6cmp_test.c
        ../keys.c
        ../network.c
        ../network.h)
target_link_libraries(ip6cmp_test ${BASE_LIBS})
add_test(ip6cmp_tt ip6cmp_test ${CMAKE_SOURCE_DIR}/tests/test_ips)

set(OUTPUT_BASE_SRC
    ../pcapdb_init.c
    ../bucketize.c
    ../index.c
    ../network.c
    ../queue.c
    ../keys.c
    ../event.c
    ../output.c
    ../db.c)
set(DB_TEST_SRC db_test.c ${OUTPUT_BASE_SRC})
add_executable(db_test ${DB_TEST_SRC})
target_link_libraries(db_test ${BASE_LIBS})
add_test(db_tt db_test)

set(IDX_HEIGHT_SRC idx_height_test.c ${OUTPUT_BASE_SRC})
add_executable(idx_height_test ${IDX_HEIGHT_SRC})
target_link_libraries(idx_height_test ${BASE_LIBS})
add_test(height_tt idx_height_test)

set(IDX_OUTPUT_SRC
    idx_output_test.c
    ../bucketize.c
    ../capture.c
    ../keys.c
    test_args.c
    ${OUTPUT_BASE_SRC})
add_executable(idx_output_test ${IDX_OUTPUT_SRC})
target_link_libraries(idx_output_test ${BASE_LIBS})
add_test(output_tt ${CMAKE_SOURCE_DIR}/tests/output_test ${CMAKE_SOURCE_DIR}/tests/enum.pcap)

# Check for memory leaks with valgrind.
set(VG_OUTPUT_T_OPTS -t ${CMAKE_SOURCE_DIR}/tests/enum.pcap)
set(VG_IDX_T_OPTS -t ${CMAKE_SOURCE_DIR}/tests/data/test.pcap)
set(VG_CMD /usr/bin/valgrind --suppressions=${CMAKE_SOURCE_DIR}/tests/tests.supp --track-origins=yes)
add_test(val_index_tt ${VG_CMD} ${CMAKE_BINARY_DIR}/tests/index_test ${VG_IDX_T_OPTS})
add_test(val_output_tt ${VG_CMD} ${CMAKE_BINARY_DIR}/tests/idx_output_test ${VG_OUTPUT_T_OPTS})

set(F2P_FCAP_PATH /tmp/capture/capture_disks/test_disk/p000000001.fcap)
add_test(fcap2pcap_tt ${CMAKE_BINARY_DIR}/fcap2pcap -r ${F2P_FCAP_PATH} -w /tmp/converted_fcap.pcap)
set_property(TEST fcap2pcap_tt APPEND PROPERTY DEPEND output_tt)
add_test(output_check_tt diff /tmp/converted_fcap.pcap ${CMAKE_SOURCE_DIR}/tests/data/converted_fcap.pcap)
set_property(TEST output_check_tt APPEND PROPERTY DEPEND fcap2pcap_tt)

set(CONSUME_TEST_SRC consume_test.c
                     ../pcapdb_init.c
                     ../capture.c
                     ../bucketize.c
                     ../network.c
                     ../keys.c
                     ../queue.c
                     ../event.c)
add_executable(consume_test_libpcap ${CONSUME_TEST_SRC})
target_link_libraries(consume_test_libpcap ${BASE_LIBS})
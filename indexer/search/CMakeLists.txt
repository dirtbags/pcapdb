cmake_minimum_required(VERSION 2.8.11)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_subdirectory(tests)

set(CMAKE_BUILD_TYPE Debug)

set(SEARCH_SOURCE
        search.c
        ordered_set.c
        search_lib.c
        subindex.c
        packets.c
        ../keys.c
        ../queue.c
        ../event.c
        ../network.c)
add_executable(search ${SEARCH_SOURCE})
target_link_libraries(search pthread rt)

set(SUBIDX_SEARCH_SOURCE
        subindex.c
        ordered_set.c
        search_lib.c
        subindex_search.c
        ../keys.c
        ../network.c)
add_executable(subindex_search ${SUBIDX_SEARCH_SOURCE})

set(AND_SOURCE
        and.c
        ordered_set.c
        search_lib.c
        ../network.c
        ../keys.c)
add_executable(and_results ${AND_SOURCE})

set(MERGE_SOURCE
        merge_flows.c
        search_lib.c
        ordered_set.c
        ../network.c
        ../keys.c)
add_executable(merge ${MERGE_SOURCE})

set(READ_FLOWS_SOURCE
        read_flows.c
        ordered_set.c
        search_lib.c
        ../network.c
        ../keys.c
)
add_executable(read_flows ${READ_FLOWS_SOURCE})

set(GENERATE_FLOWS_SOURCE
        tests/flow_generator.c
        ordered_set.c
        search_lib.c
        ../network.c
        ../keys.c
)
add_executable(generate_flows ${GENERATE_FLOWS_SOURCE})

set(FLOW_FETCH_SOURCE
        flow_fetch.c
        ordered_set.c
        search_lib.c
        ../network.c
        ../keys.c
)
add_executable(flow_fetch ${FLOW_FETCH_SOURCE})

set(PKT_FETCH_SOURCE
        pcap_fetch.c
        packets.c
        ordered_set.c
        search_lib.c
        ../network.c
        ../keys.c
)
add_executable(pcap_fetch ${PKT_FETCH_SOURCE})

set(PRINT_FLOWS_SOURCE
        flow_idx_print.c
        ordered_set.c
        search_lib.c
        ../network.c
        ../keys.c
        )
add_executable(flow_idx_print ${PRINT_FLOWS_SOURCE})

add_executable(sub_index_check
        ../search/sub_index_check.c
        ../event.c
        ../queue.c
        ../db.c
        ../network.c
        ../index.c
        ../output.c
        ../keys.c)
target_link_libraries(sub_index_check pcap pq)

add_executable(flow_index_check
        ../search/flow_index_check.c
        ../network.c
        ../keys.c)
target_link_libraries(flow_index_check pcap)

add_executable(flow_set_print
        flow_set_print.c
        ordered_set.c
        ../keys.c
        ../network.c)

install(TARGETS search subindex_search and_results merge flow_fetch pcap_fetch sub_index_check
                flow_index_check read_flows flow_set_print
        RUNTIME DESTINATION bin)

cmake_minimum_required(VERSION 2.8.11)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
set(CMAKE_BUILD_TYPE Debug)

enable_testing()

set(VERSION_MAJOR 0)
execute_process(COMMAND git rev-list HEAD --buffer_items OUTPUT_VARIABLE VERSION_MINOR WORKING_DIRECTORY .)
set(CMAKE_INSTALL_PREFIX /)

project(cornet_indexer)

set(GLOBAL_SOURCE pcapdb.h)

include_directories("/usr/local/include")

find_package(Threads REQUIRED)
find_package(PCAP REQUIRED)
include_directories(${PCAP_INCLUDE_DIRS})
find_package(HugeTLBFS REQUIRED)
include_directories(${HUGETLBFS_INCLUDE_DIRS})
set(PostgreSQL_ADDITIONAL_VERSIONS "9.4")
# Workaround for the RHEL postgres install
if(EXISTS /usr/pgsql-9.4/)
    set(PostgreSQL_ROOT /usr/pgsql-9.4/)
    set(PostgreSQL_INCLUDE_DIR /usr/pgsql-9.4/include)
    set(PostgreSQL_LIBRARY_DIR /usr/pgsql-9.4/lib)
endif(EXISTS /usr/pgsql-9.4/)
# Workaround for the debian postgres install.
if(EXISTS /var/lib/postgresql/9.4/)
    set(PostgreSQL_ROOT /usr/include/postgresql/9.4/server)
    set(PostgreSQL_INCLUDE_DIR /usr/include/postgresql/9.4)
    set(PostgreSQL_LIBRARY_DIR /var/lib/postgresql/9.4)
endif(EXISTS /var/lib/postgresql/9.4/)
include_directories(${PostgreSQL_INCLUDE_DIRS})

find_package(PostgreSQL REQUIRED)
link_directories(${PostgreSQL_LIBRARY_DIRS})
# NTOP include directories.
find_package(PFRing REQUIRED)
include_directories(${PFRING_INCLUDE_DIRS})
find_package(NUMA REQUIRED)


set(CAPTURE_SOURCE
    pcapdb.c
    pcapdb_init.c
    capture.c
    bucketize.c
    db.c
    event.c
    keys.c
    index.c
    network.c
    output.c
    queue.c)
add_executable(capture ${CAPTURE_SOURCE})
target_link_libraries(capture pq pcap hugetlbfs pthread pfring numa)

add_executable(fcap2pcap fcap2pcap.c)
target_link_libraries(fcap2pcap pcap)

add_executable(show_keys show_keys.c keys.c network.c)

install(TARGETS capture fcap2pcap show_keys
    RUNTIME DESTINATION bin)

set(SCRIPTS
    scripts/core_count
    scripts/count_free_hugepages
    scripts/set_interrupt_affinity)
install(PROGRAMS ${SCRIPTS}
    DESTINATION bin)

add_subdirectory(tests)
add_subdirectory(search)

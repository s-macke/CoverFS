set(CMAKE_LEGACY_CYGWIN_WIN32 0)

project (CoverFS)

cmake_minimum_required(VERSION 3.0.0)
cmake_policy(SET CMP0054 NEW)

set(PROJECT_VERSION "0.9")
set(ARCHIVE_NAME ${CMAKE_PROJECT_NAME}-${PROJECT_VERSION})

MESSAGE( STATUS "CMAKE_SYSTEM_NAME: " ${CMAKE_SYSTEM_NAME} )

set( CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake )

add_definitions(-std=c++11 -Wall -O2 -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE)

set(CPP_FILES
    src/CBlockIO.cpp
    src/CCacheIO.cpp
    src/CDirectory.cpp
    src/CEncrypt.cpp
    src/CNetBlockIO.cpp
    src/CSimpleFS.cpp
    src/CPrintCheckRepair.cpp
    src/CNetReadWriteBuffer.cpp
    src/INode.cpp
    src/debug.cpp
    src/ParallelTest.cpp
    src/CStatusView.cpp
    src/main.cpp
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(Boost COMPONENTS system REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Libgcrypt REQUIRED)


if ("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
    MESSAGE( STATUS "Add FUSE Library" )
    set(CPP_FILES ${CPP_FILES} src/fuseoper.cpp)
    find_package(fuse REQUIRED)
    set(FUSE_LIB fuse)
endif ()
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "CYGWIN")
    MESSAGE( STATUS "Add Dokan Library")
    set(CPP_FILES ${CPP_FILES} src/dokanoper.cpp)
    add_definitions(-I/cygdrive/c/Program\\ Files/Dokan/Dokan\\ Library-1.0.3/include)
    set(FUSE_LIB dokan1)
    LINK_DIRECTORIES(/cygdrive/c/Program\ Files/Dokan/Dokan\ Library-1.0.3/)
endif ()

include_directories(${Boost_INCLUDE_DIRS})

add_executable(coverfsserver src/coverfsserver.cpp)
add_executable(coverfs ${CPP_FILES})
add_executable(checkfragment tests/checkfragment.cpp)

target_link_libraries (coverfs ${Boost_SYSTEM_LIBRARY_RELEASE} pthread ssl crypto gcrypt ${FUSE_LIB})
target_link_libraries (coverfsserver ${Boost_SYSTEM_LIBRARY_RELEASE} pthread ssl crypto)
target_link_libraries (checkfragment ${Boost_SYSTEM_LIBRARY_RELEASE} boost_thread pthread)


add_custom_target(dist
    COMMAND git archive --prefix=${ARCHIVE_NAME}/ HEAD
        | bzip2 > ${CMAKE_BINARY_DIR}/${ARCHIVE_NAME}.tar.bz2
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})


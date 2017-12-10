set(CMAKE_LEGACY_CYGWIN_WIN32 0)

project (CoverFS)

cmake_minimum_required(VERSION 3.0.0)
cmake_policy(SET CMP0054 NEW)

set(PROJECT_VERSION "0.91")

enable_language(CXX)

set(ARCHIVE_NAME ${CMAKE_PROJECT_NAME}-${PROJECT_VERSION})

MESSAGE( STATUS "CMAKE_SYSTEM_NAME: " ${CMAKE_SYSTEM_NAME} )

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake )

add_definitions(-std=c++11 -Wall -O2 -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE)

set(CPP_FILES
    src/utils/Logger.cpp
    src/IO/CBlockIO.cpp
    src/IO/CCacheIO.cpp
    src/IO/CEncrypt.cpp
    src/IO/CNetBlockIO.cpp
    src/IO/CNetReadWriteBuffer.cpp
    src/SimpleFS/CSimpleFS.cpp
    src/SimpleFS/CDirectory.cpp
    src/SimpleFS/INode.cpp
    src/SimpleFS/CFragment.cpp
    src/SimpleFS/CPrintCheckRepair.cpp
    src/ParallelTest.cpp
    src/CStatusView.cpp
    src/main.cpp
    src/CFSHandler.cpp
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(Boost COMPONENTS system REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Libgcrypt REQUIRED)
find_package(POCO)


if ("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
    MESSAGE( STATUS "Add FUSE Library" )
    set(CPP_FILES ${CPP_FILES} src/fuse/fuseoper.cpp)
    find_package(fuse REQUIRED)
    set(FUSE_LIB fuse)
endif ()
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "CYGWIN")
    MESSAGE( STATUS "Add Dokan Library")
    set(CPP_FILES ${CPP_FILES} src/fuse/dokanoper.cpp)
    add_definitions(-I/cygdrive/c/Program\\ Files/Dokan/Dokan\\ Library-1.0.3/include)
    set(FUSE_LIB dokan1)
    LINK_DIRECTORIES(/cygdrive/c/Program\ Files/Dokan/Dokan\ Library-1.0.3/)
endif ()

include_directories(${Boost_INCLUDE_DIRS} ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/src/utils)

if (POCO_FOUND)
    set(HAVE_POCO TRUE)
    set(POCO_LIB PocoNet PocoUtil PocoFoundation PocoXML)
    set(CPP_FILES ${CPP_FILES} src/webapp/CConfigFile.cpp src/webapp/webapp.cpp)
endif ()

configure_file(
    ${PROJECT_SOURCE_DIR}/cmake/config.h.in
    ${PROJECT_BINARY_DIR}/config.h
)

add_executable(coverfsserver src/coverfsserver.cpp src/utils/Logger.cpp)
add_executable(coverfs ${CPP_FILES})
add_executable(checkfragment tests/checkfragment.cpp)

target_link_libraries (coverfs ${Boost_SYSTEM_LIBRARY_RELEASE} pthread ssl crypto gcrypt ${FUSE_LIB} ${POCO_LIB})
target_link_libraries (coverfsserver ${Boost_SYSTEM_LIBRARY_RELEASE} pthread ssl crypto)
target_link_libraries (checkfragment ${Boost_SYSTEM_LIBRARY_RELEASE} boost_thread pthread)

add_custom_command(
    TARGET coverfs POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E create_symlink ${PROJECT_SOURCE_DIR}/ssl ${PROJECT_BINARY_DIR}/ssl
    COMMAND "${CMAKE_COMMAND}" -E create_symlink ${PROJECT_SOURCE_DIR}/templates ${PROJECT_BINARY_DIR}/templates
    COMMENT "Copying to build directory"
)


add_custom_target(dist
    COMMAND git archive --prefix=${ARCHIVE_NAME}/ HEAD
        | bzip2 > ${CMAKE_BINARY_DIR}/${ARCHIVE_NAME}.tar.bz2
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})


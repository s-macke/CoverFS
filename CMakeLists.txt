set(CMAKE_LEGACY_CYGWIN_WIN32 0)

#MINGW64 compile with  CXX=g++ CC=gcc cmake ..

project (CoverFS)
set(PROJECT_VERSION "0.91")

cmake_minimum_required(VERSION 3.0.0)
cmake_policy(SET CMP0054 NEW)


enable_language(CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED on)

set(ARCHIVE_NAME ${CMAKE_PROJECT_NAME}-${PROJECT_VERSION})


MESSAGE( STATUS "CMAKE_SYSTEM_NAME: " ${CMAKE_SYSTEM_NAME} )

if (WIN32)
    MESSAGE( STATUS "SYSTEM: WIN32")
endif ()
if (UNIX)
    MESSAGE( STATUS "SYSTEM: UNIX")
endif ()
if (CYGWIN)
    MESSAGE( STATUS "SYSTEM: CYGWIN")
endif ()
if (MINGW)
    MESSAGE( STATUS "SYSTEM: MINGW")
endif ()
if (MSYS)
    MESSAGE( STATUS "SYSTEM: MSYS")
endif ()
if (APPLE)
    MESSAGE( STATUS "SYSTEM: APPLE")
endif ()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake )

add_definitions(-std=c++14 -Wall -O2 -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE)

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
    src/interface/CFSHandler.cpp
    src/client/ParallelTest.cpp
    src/client/CStatusView.cpp
    src/client/main.cpp
    )

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(Boost COMPONENTS system REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Libgcrypt REQUIRED)
find_package(POCO)

if ("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
    MESSAGE( STATUS "Add FUSE Library" )
    find_package(fuse REQUIRED)
    add_definitions(${FUSE_DEFINITIONS})
    include_directories(${FUSE_INCLUDE_DIRS})
    set(CPP_FILES ${CPP_FILES} src/fuse/fuseoper.cpp)
    set(FUSE_LIB ${FUSE_LIBRARIES})
endif ()
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "CYGWIN")
    MESSAGE( STATUS "Add Dokan Library")
    find_package(dokan REQUIRED)
    set(CPP_FILES ${CPP_FILES} src/fuse/dokanoper.cpp)
    include_directories(${DOKAN_INCLUDE_DIR})
    set(FUSE_LIB ${DOKAN_LIB_DIR})
endif ()
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "MSYS" OR "${CMAKE_SYSTEM_NAME}" STREQUAL "MINGW" OR "${CMAKE_SYSTEM_NAME}" STREQUAL "MINGW64_NT-10.0")
    MESSAGE( STATUS "Add Dokan Library")
    find_package(dokan REQUIRED)
    set(CPP_FILES ${CPP_FILES} src/fuse/dokanoper.cpp)
    include_directories(${DOKAN_INCLUDE_DIR})
    set(FUSE_LIB ${DOKAN_LIB_DIR})
    set(PLATFORM_LIBS ws2_32 wsock32)
endif ()


include_directories(${Boost_INCLUDE_DIRS} ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/src/utils)

if (Poco_FOUND)
    set(HAVE_POCO TRUE)
    set(POCO_LIB PocoNet PocoUtil PocoFoundation PocoXML)
    set(CPP_FILES ${CPP_FILES} src/webapp/CConfigFile.cpp src/webapp/webapp.cpp)
    add_definitions(-DPOCO_WIN32_UTF8)
endif ()

configure_file(
    ${PROJECT_SOURCE_DIR}/cmake/config.h.in
    ${PROJECT_BINARY_DIR}/config.h
)

add_executable(coverfsserver src/server/coverfsserver.cpp src/utils/Logger.cpp)
add_executable(coverfs ${CPP_FILES})
add_executable(checkfragment tests/checkfragment.cpp)

target_link_libraries (coverfs ${Boost_SYSTEM_LIBRARY_RELEASE} pthread ssl crypto gcrypt ${FUSE_LIB} ${POCO_LIB} ${PLATFORM_LIBS})
target_link_libraries (coverfsserver ${Boost_SYSTEM_LIBRARY_RELEASE} pthread ssl crypto ${PLATFORM_LIBS})
target_link_libraries (checkfragment ${Boost_SYSTEM_LIBRARY_RELEASE} pthread ${PLATFORM_LIBS})

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


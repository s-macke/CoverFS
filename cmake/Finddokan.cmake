    #set(DOKAN_ROOT_PATHS "$ENV{PROGRAMFILES}/Dokan")

    #MESSAGE( STATUS "DOKAN_ROOT_PATH: " ${DOKAN_ROOT_PATHS} )

    set(DOKAN_FOUND TRUE)

    set(DOKAN_ROOT_PATHS    "/cygdrive/c/Program Files/Dokan/Dokan Library-1.0.3"
            "/c/Program Files/Dokan/Dokan Library-1.0.3"
            "c:/Program Files/Dokan/Dokan Library-1.0.3")

    find_path(DOKAN_INCLUDE_DIR
            NAMES
            dokan/dokan.h
            PATHS
            ${DOKAN_ROOT_PATHS}
            PATH_SUFFIXES
            include
            )


    if(NOT DOKAN_INCLUDE_DIRS)
        set(DOKAN_FOUND FALSE)
    endif(NOT DOKAN_INCLUDE_DIRS)

    SET(CMAKE_FIND_LIBRARY_PREFIXES "")
    SET(CMAKE_FIND_LIBRARY_SUFFIXES ".lib" ".dll")

    find_library(DOKAN_LIB_DIR
            NAMES
            dokan1
            PATHS
            ${DOKAN_ROOT_PATHS}
            )

    if(NOT DOKAN_LIB_DIRS)
        set(DOKAN_FOUND FALSE)
    endif(NOT DOKAN_LIB_DIRS)

    MESSAGE( STATUS "DOKAN_INCLUDE_DIR: " ${DOKAN_INCLUDE_DIR} )
    MESSAGE( STATUS "DOKAN_LIB_DIR: " ${DOKAN_LIB_DIR} )
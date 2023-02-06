CMAKE_MINIMUM_REQUIRED(VERSION 3.10)

PROJECT(crypto_infra)

#FIND_PACKAGE(Arrow 9 QUIET REQUIRED)
#FIND_PACKAGE(Boost 1.79 REQUIRED)
if("${CONDA_PREFIX}" STREQUAL "")
    LINK_DIRECTORIES("/usr/local/lib/")
else("${CONDA_PREFIX}" STREQUAL "")
    message("CONDA_PREFIX=${CONDA_PREFIX}")
    LINK_DIRECTORIES("${CONDA_PREFIX}/lib/")
endif()

#message("libamber_source=${LIBAMBER_SOURCE}")

SET(DEFAULT_BUILD_TYPE "DEBUG")
SET(CMAKE_CXX_STANDARD 20)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(CMAKE_CXX_FLAGS "-Werror -Wall -Wno-error=deprecated-declarations -Wno-error=deprecated -Wno-error=unused-function  -Wno-sign-compare -Wno-error=maybe-uninitialized -Wno-error=stringop-truncation -Wno-free-nonheap-object -Wno-error=comment -fext-numeric-literals -pthread ${CMAKE_CXX_FLAGS}")
SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wno-error=strict-overflow -Wno-error=strict-aliasing -O3")
SET(CMAKE_CXX_FLAGS_EXTRA "-pedantic -Wall -Wextra -Wcast-align -Wdisabled-optimization -Wformat=2 -Winit-self -Wlogical-op -Wmissing-include-dirs -Wnoexcept -Woverloaded-virtual -Wredundant-decls -Wstrict-null-sentinel -Wundef -Wno-unused -Wl,--as-needed")

SET(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
SET(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/)




#ADD_SUBDIRECTORY("${PROJECT_SOURCE_DIR}/libnano")

############### APPLICATION ##################
MACRO(ADD_APPLICATION APP_NAME)
    SET(TARGET_NAME ${APP_NAME})

    FILE(GLOB SRC
        ${PROJECT_SOURCE_DIR}/${APP_NAME}/*.h
        ${PROJECT_SOURCE_DIR}/${APP_NAME}/*.cpp
        ${PROJECT_SOURCE_DIR}/${APP_NAME}/**/*.h
        ${PROJECT_SOURCE_DIR}/${APP_NAME}/**/*.cpp
    )

    ADD_EXECUTABLE(${TARGET_NAME} ${SRC})
    include_directories("${CONDA_PREFIX}/include/")

        TARGET_INCLUDE_DIRECTORIES(${TARGET_NAME} PUBLIC
            "${PROJECT_SOURCE_DIR}"
            "${PROJECT_SOURCE_DIR}/${APP_NAME}"
	        "${PROJECT_SOURCE_DIR}/libnano"
        )
        #TARGET_LINK_LIBRARIES(${TARGET_NAME}
            # internal lib
         #   -Wl,--start-group
         #   -Wl,--end-group
        #)
  

   
    #TARGET_LINK_LIBRARIES(${TARGET_NAME}
    #)
    

ENDMACRO()

ADD_APPLICATION(smb_manager)

CMAKE_MINIMUM_REQUIRED(VERSION 3.2)

PROJECT(texpacker C)

OPTION(TEXPACKER_INSTALL "TEXPACKER_INSTALL" OFF)

set(TEXPACKER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/texpacker.c
)

set(TEXPACKER_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/texpacker/texpacker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/texpacker/texpacker.hpp
)

include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/include)

include_directories(${PROJECT_NAME} ${TEXPACKER_THIRDPARTY_CONFIG_DIR}/jansson/include)
include_directories(${PROJECT_NAME} ${TEXPACKER_THIRDPARTY_DIR}/jansson/include)

include_directories(${PROJECT_NAME} ${TEXPACKER_THIRDPARTY_DIR}/stb/)

add_executable(${PROJECT_NAME} ${TEXPACKER_SOURCES} ${TEXPACKER_HEADERS})

target_link_libraries(${PROJECT_NAME} ${TEXPACKER_THIRDPARTY_LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}jansson${CMAKE_STATIC_LIBRARY_SUFFIX})

if(TEXPACKER_INSTALL)
    install(DIRECTORY include
        DESTINATION .
        FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")

    install(TARGETS ${PROJECT_NAME}
            LIBRARY DESTINATION lib
            ARCHIVE DESTINATION lib)
endif()


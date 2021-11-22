CMAKE_MINIMUM_REQUIRED(VERSION 3.10)

OPTION(TEXPACKER_DOWNLOADS_SILENT "download silent" OFF)

PROJECT(Downloads)

SET(TEXPACKER_REPOSITORY ${CMAKE_CURRENT_SOURCE_DIR}/../..)
SET(TEXPACKER_THIRDPARTY_DIR ${TEXPACKER_REPOSITORY}/dependencies)

include(${CMAKE_CURRENT_SOURCE_DIR}/../download_template.cmake)

GIT_CLONE(jansson https://github.com/akheron/jansson.git "v2.14")
GIT_CLONE(stb https://github.com/nothings/stb.git)

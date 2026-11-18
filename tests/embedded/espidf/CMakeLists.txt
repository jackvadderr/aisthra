cmake_minimum_required(VERSION 3.16)

# This is a standalone ESP-IDF project rooted inside the repository. The
# framework is supplied by the environment, normally /usr/share/esp-idf on
# the Gentoo installation used for validation.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(aisthra_esp32_validation)

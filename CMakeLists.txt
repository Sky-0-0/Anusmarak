cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(EXTRA_COMPONENT_DIRS "Utilities")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(Anusmarak)

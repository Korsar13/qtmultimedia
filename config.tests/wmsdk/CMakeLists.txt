# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

# Generated from wmsdk.pro.

cmake_minimum_required(VERSION 3.16)
project(config_test_wmsdk LANGUAGES C CXX)

if(DEFINED QT_CONFIG_COMPILE_TEST_CMAKE_SYSTEM_PREFIX_PATH)
    set(CMAKE_SYSTEM_PREFIX_PATH "${QT_CONFIG_COMPILE_TEST_CMAKE_SYSTEM_PREFIX_PATH}")
endif()
if(DEFINED QT_CONFIG_COMPILE_TEST_CMAKE_SYSTEM_FRAMEWORK_PATH)
    set(CMAKE_SYSTEM_FRAMEWORK_PATH "${QT_CONFIG_COMPILE_TEST_CMAKE_SYSTEM_FRAMEWORK_PATH}")
endif()

foreach(p ${QT_CONFIG_COMPILE_TEST_PACKAGES})
    find_package(${p})
endforeach()

if(QT_CONFIG_COMPILE_TEST_LIBRARIES)
    link_libraries(${QT_CONFIG_COMPILE_TEST_LIBRARIES})
endif()
if(QT_CONFIG_COMPILE_TEST_LIBRARY_TARGETS)
    foreach(lib ${QT_CONFIG_COMPILE_TEST_LIBRARY_TARGETS})
        if(TARGET ${lib})
            link_libraries(${lib})
        endif()
    endforeach()
endif()

add_executable(${PROJECT_NAME}
    main.cpp
)

# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

# Generated from unit.pro.

add_subdirectory(mockbackend)
add_subdirectory(multimedia)
if(TARGET Qt::Widgets)
    add_subdirectory(multimediawidgets)
endif()

# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

# Generated from src.pro.

add_subdirectory(resonance-audio)
add_subdirectory(multimedia)
if(ANDROID)
    add_subdirectory(android)
endif()
if (QT_FEATURE_spatialaudio)
    add_subdirectory(spatialaudio)
endif()

# special case begin
if(TARGET Qt::Quick)
    add_subdirectory(multimediaquick)
endif()
if(TARGET Qt::Quick3D AND QT_FEATURE_spatialaudio)
    add_subdirectory(spatialaudioquick3d)
endif()

if(TARGET Qt::Widgets)
    add_subdirectory(multimediawidgets)
endif()

add_subdirectory(plugins)
#special case end

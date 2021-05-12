/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QMEDIADEVICES_H
#define QMEDIADEVICES_H

#include <QtMultimedia/qtmultimediaglobal.h>
#include <QtCore/qobject.h>
#include <QtCore/qstringlist.h>

QT_BEGIN_NAMESPACE

class QAudioDeviceInfo;
class QCameraInfo;

class QMediaDevicesPrivate;

class Q_MULTIMEDIA_EXPORT QMediaDevices : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QList<QAudioDeviceInfo> audioInputs READ audioInputs NOTIFY audioInputsChanged)
    Q_PROPERTY(QList<QAudioDeviceInfo> audioOutputs READ audioOutputs NOTIFY audioOutputsChanged)
    Q_PROPERTY(QList<QCameraInfo> videoInputs READ videoInputs NOTIFY videoInputsChanged)
    Q_PROPERTY(QAudioDeviceInfo defaultAudioInput READ defaultAudioInput NOTIFY audioInputsChanged)
    Q_PROPERTY(QAudioDeviceInfo defaultAudioOutput READ defaultAudioOutput NOTIFY audioOutputsChanged)
    Q_PROPERTY(QCameraInfo defaultVideoInput READ defaultVideoInput NOTIFY videoInputsChanged)

public:
    QMediaDevices(QObject *parent = nullptr);
    ~QMediaDevices();

    static QList<QAudioDeviceInfo> audioInputs();
    static QList<QAudioDeviceInfo> audioOutputs();
    static QList<QCameraInfo> videoInputs();

    static QAudioDeviceInfo defaultAudioInput();
    static QAudioDeviceInfo defaultAudioOutput();
    static QCameraInfo defaultVideoInput();

signals:
    void audioInputsChanged();
    void audioOutputsChanged();
    void videoInputsChanged();

private:
    friend class QMediaDevicesPrivate;
};

QT_END_NAMESPACE


#endif  // QABSTRACTMEDIASERVICE_H

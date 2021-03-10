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

#ifndef QGSTREAMERIMAGEPROCESSING_P_H
#define QGSTREAMERIMAGEPROCESSING_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <QtMultimedia/private/qtmultimediaglobal_p.h>
#include <qcamera.h>
#include <private/qplatformcameraimageprocessing_p.h>

#include <private/qgst_p.h>

#if QT_CONFIG(gstreamer_photography)
# include <gst/interfaces/photography.h>
#endif

QT_BEGIN_NAMESPACE

#if QT_CONFIG(linux_v4l)
class QGstreamerImageProcessingV4L2;
#endif

class QGstreamerCamera;

class QGstreamerImageProcessing : public QPlatformCameraImageProcessing
{
    Q_OBJECT

public:
    QGstreamerImageProcessing(QGstreamerCamera *camera);
    virtual ~QGstreamerImageProcessing();

    QCameraImageProcessing::WhiteBalanceMode whiteBalanceMode() const;
    bool setWhiteBalanceMode(QCameraImageProcessing::WhiteBalanceMode mode);
    bool isWhiteBalanceModeSupported(QCameraImageProcessing::WhiteBalanceMode mode) const;

    QCameraImageProcessing::ColorFilter colorFilter() const;
    bool setColorFilter(QCameraImageProcessing::ColorFilter filter);
    bool isColorFilterSupported(QCameraImageProcessing::ColorFilter mode) const;

    bool isParameterSupported(ProcessingParameter) const override;
    bool isParameterValueSupported(ProcessingParameter parameter, const QVariant &value) const override;
    QVariant parameter(ProcessingParameter parameter) const override;
    void setParameter(ProcessingParameter parameter, const QVariant &value) override;

    void update();

private:
    bool setColorBalanceValue(const char *channel, qreal value);
    void updateColorBalanceValues();

private:
    QGstreamerCamera *m_camera;
    QMap<QPlatformCameraImageProcessing::ProcessingParameter, int> m_values;
    QCameraImageProcessing::WhiteBalanceMode m_whiteBalanceMode = QCameraImageProcessing::WhiteBalanceAuto;
    QCameraImageProcessing::ColorFilter m_colorFilter = QCameraImageProcessing::ColorFilterNone;

#if QT_CONFIG(linux_v4l)
    bool isV4L2Device = false;
    void updateV4L2Controls();
    std::optional<float> getV4L2Param(QGstreamerImageProcessing::ProcessingParameter param) const;
    bool setV4L2Param(ProcessingParameter parameter, const QVariant &value);

public:
    struct SourceParameterValueInfo {
        quint32 cid = 0; // V4L control id
        qint32 defaultValue = 0;
        qint32 minimumValue = 0;
        qint32 maximumValue = 0;
    };
private:
    bool v4l2AutoWhiteBalanceSupported = false;
    bool v4l2ColorTemperatureSupported = false;
    SourceParameterValueInfo v4l2ColorTemperature;
    SourceParameterValueInfo v4l2Brightness;
    SourceParameterValueInfo v4l2Contrast;
    SourceParameterValueInfo v4l2Saturation;
#endif
};

QT_END_NAMESPACE

#endif // QGSTREAMERIMAGEPROCESSING_P_H
/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QDECLARATIVECAMERARECORDER_H
#define QDECLARATIVECAMERARECORDER_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists for the convenience
// of other Qt classes.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <qcamera.h>
#include <qmediarecorder.h>
#include <qmediaencodersettings.h>

QT_BEGIN_HEADER

QT_BEGIN_NAMESPACE

class QDeclarativeCamera;

class QDeclarativeCameraRecorder : public QObject
{
    Q_OBJECT
    Q_ENUMS(RecorderState)
    Q_ENUMS(RecorderStatus)
    Q_ENUMS(EncodingMode)

    Q_PROPERTY(RecorderState recorderState READ recorderState WRITE setRecorderState NOTIFY recorderStateChanged)
    Q_PROPERTY(RecorderStatus recorderStatus READ recorderStatus NOTIFY recorderStatusChanged)

    Q_PROPERTY(QString videoCodec READ videoCodec WRITE setVideoCodec NOTIFY videoCodecChanged)
    Q_PROPERTY(QSize resolution READ captureResolution WRITE setCaptureResolution NOTIFY captureResolutionChanged)
    Q_PROPERTY(qreal frameRate READ frameRate WRITE setFrameRate NOTIFY frameRateChanged)
    Q_PROPERTY(int videoBitRate READ videoBitRate WRITE setVideoBitRate NOTIFY videoBitRateChanged)
    Q_PROPERTY(EncodingMode videoEncodingMode READ videoEncodingMode WRITE setVideoEncodingMode NOTIFY videoEncodingModeChanged)

    Q_PROPERTY(QString audioCodec READ audioCodec WRITE setAudioCodec NOTIFY audioCodecChanged)
    Q_PROPERTY(int audioBitRate READ audioBitRate WRITE setAudioBitRate NOTIFY audioBitRateChanged)
    Q_PROPERTY(int audioChannels READ audioChannels WRITE setAudioChannels NOTIFY audioChannelsChanged)
    Q_PROPERTY(int audioSampleRate READ audioSampleRate WRITE setAudioSampleRate NOTIFY audioSampleRateChanged)
    Q_PROPERTY(EncodingMode audioEncodingMode READ audioEncodingMode WRITE setAudioEncodingMode NOTIFY audioEncodingModeChanged)

    Q_PROPERTY(QString mediaContainer READ mediaContainer WRITE setMediaContainer NOTIFY mediaContainerChanged)

    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(QString outputLocation READ outputLocation WRITE setOutputLocation NOTIFY outputLocationChanged)
    Q_PROPERTY(QString actualLocation READ actualLocation NOTIFY actualLocationChanged)
    Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY error)

public:
    enum RecorderState
    {
        StoppedState = QMediaRecorder::StoppedState,
        RecordingState = QMediaRecorder::RecordingState
    };

    enum RecorderStatus
    {
        UnavailableStatus = QMediaRecorder::UnavailableStatus,
        UnloadedStatus = QMediaRecorder::UnloadedStatus,
        LoadingStatus = QMediaRecorder::LoadingStatus,
        LoadedStatus = QMediaRecorder::LoadedStatus,
        StartingStatus = QMediaRecorder::StartingStatus,
        RecordingStatus = QMediaRecorder::RecordingStatus,
        PausedStatus = QMediaRecorder::PausedStatus,
        FinalizingStatus = QMediaRecorder::FinalizingStatus
    };

    enum EncodingMode
    {
        ConstantQualityEncoding = QtMultimedia::ConstantQualityEncoding,
        ConstantBitRateEncoding = QtMultimedia::ConstantBitRateEncoding,
        AverageBitRateEncoding = QtMultimedia::AverageBitRateEncoding
    };

    ~QDeclarativeCameraRecorder();

    RecorderState recorderState() const;
    RecorderStatus recorderStatus() const;

    QSize captureResolution();

    QString outputLocation() const;
    QString actualLocation() const;

    qint64 duration() const;
    bool isMuted() const;

    QString audioCodec() const;
    QString videoCodec() const;
    QString mediaContainer() const;

    QMediaRecorder::Error error() const;
    QString errorString() const;

    qreal frameRate() const;
    int videoBitRate() const;
    int audioBitRate() const;
    int audioChannels() const;
    int audioSampleRate() const;

    EncodingMode videoEncodingMode() const;
    EncodingMode audioEncodingMode() const;

public Q_SLOTS:
    void setOutputLocation(const QString &location);

    void record();
    void stop();
    void setRecorderState(QDeclarativeCameraRecorder::RecorderState state);

    void setMuted(bool muted);
    void setMetadata(const QString &key, const QVariant &value);

    void setCaptureResolution(const QSize &resolution);
    void setAudioCodec(const QString &codec);
    void setVideoCodec(const QString &codec);
    void setMediaContainer(const QString &container);

    void setFrameRate(qreal frameRate);
    void setVideoBitRate(int rate);
    void setAudioBitRate(int rate);
    void setAudioChannels(int channels);
    void setAudioSampleRate(int rate);

    void setVideoEncodingMode(EncodingMode encodingMode);
    void setAudioEncodingMode(EncodingMode encodingMode);

Q_SIGNALS:
    void recorderStateChanged(QDeclarativeCameraRecorder::RecorderState state);
    void recorderStatusChanged();
    void durationChanged(qint64 duration);
    void mutedChanged(bool muted);
    void outputLocationChanged(const QString &location);
    void actualLocationChanged(const QString &location);

    void error(QMediaRecorder::Error errorCode);

    void metaDataChanged(const QString &key, const QVariant &value);

    void captureResolutionChanged(const QSize &);
    void audioCodecChanged(const QString &codec);
    void videoCodecChanged(const QString &codec);
    void mediaContainerChanged(const QString &container);

    void frameRateChanged(qreal arg);
    void videoBitRateChanged(int arg);
    void audioBitRateChanged(int arg);
    void audioChannelsChanged(int arg);
    void audioSampleRateChanged(int arg);

    void audioEncodingModeChanged(EncodingMode encodingMode);
    void videoEncodingModeChanged(EncodingMode encodingMode);

private slots:
    void updateRecorderState(QMediaRecorder::State);
    void updateRecorderError(QMediaRecorder::Error);
    void updateActualLocation(const QUrl&);

private:
    friend class QDeclarativeCamera;
    QDeclarativeCameraRecorder(QCamera *camera, QObject *parent = 0);

    QMediaRecorder *m_recorder;

    QAudioEncoderSettings m_audioSettings;
    QVideoEncoderSettings m_videoSettings;
    QString m_mediaContainer;
};

QT_END_NAMESPACE

QML_DECLARE_TYPE(QT_PREPEND_NAMESPACE(QDeclarativeCameraRecorder))

QT_END_HEADER

#endif

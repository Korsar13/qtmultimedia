/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/
**
** This file is part of the Qt Toolkit.
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

#include "qgstreamerrecordercontrol.h"
#include "qgstreameraudioencode.h"
#include "qgstreamervideoencode.h"
#include "qgstreamermediacontainercontrol.h"
#include <QtCore/QDebug>

QGstreamerRecorderControl::QGstreamerRecorderControl(QGstreamerCaptureSession *session)
    :QMediaRecorderControl(session),
      m_session(session),
      m_state(QMediaRecorder::StoppedState),
      m_status(QMediaRecorder::UnloadedStatus)
{
    connect(m_session, SIGNAL(stateChanged(QGstreamerCaptureSession::State)), SLOT(updateStatus()));
    connect(m_session, SIGNAL(error(int,QString)), SLOT(handleSessionError(int,QString)));
    connect(m_session, SIGNAL(durationChanged(qint64)), SIGNAL(durationChanged(qint64)));
    connect(m_session, SIGNAL(mutedChanged(bool)), SIGNAL(mutedChanged(bool)));
    m_hasPreviewState = m_session->captureMode() != QGstreamerCaptureSession::Audio;
}

QGstreamerRecorderControl::~QGstreamerRecorderControl()
{
}

QUrl QGstreamerRecorderControl::outputLocation() const
{
    return m_session->outputLocation();
}

bool QGstreamerRecorderControl::setOutputLocation(const QUrl &sink)
{
    m_outputLocation = sink;
    m_session->setOutputLocation(sink);
    return true;
}


QMediaRecorder::State QGstreamerRecorderControl::state() const
{
    return m_state;
}

QMediaRecorder::Status QGstreamerRecorderControl::status() const
{
    static QMediaRecorder::Status statusTable[3][3] = {
        //Stopped recorder state:
        { QMediaRecorder::LoadedStatus, QMediaRecorder::FinalizingStatus, QMediaRecorder::FinalizingStatus },
        //Recording recorder state:
        { QMediaRecorder::StartingStatus, QMediaRecorder::RecordingStatus, QMediaRecorder::PausedStatus },
        //Paused recorder state:
        { QMediaRecorder::StartingStatus, QMediaRecorder::RecordingStatus, QMediaRecorder::PausedStatus }
    };

    QMediaRecorder::State sessionState = QMediaRecorder::StoppedState;

    switch ( m_session->state() ) {
        case QGstreamerCaptureSession::RecordingState:
            sessionState = QMediaRecorder::RecordingState;
        case QGstreamerCaptureSession::PausedState:
            sessionState = QMediaRecorder::PausedState;
        case QGstreamerCaptureSession::PreviewState:
        case QGstreamerCaptureSession::StoppedState:
            sessionState = QMediaRecorder::StoppedState;
    }

    return statusTable[m_state][sessionState];
}

void QGstreamerRecorderControl::updateStatus()
{
    QMediaRecorder::Status newStatus = status();
    if (m_status != newStatus) {
        m_status = newStatus;
        emit statusChanged(m_status);
    }
}

void QGstreamerRecorderControl::handleSessionError(int code, const QString &description)
{
    emit error(code, description);
    stop();
}

qint64 QGstreamerRecorderControl::duration() const
{
    return m_session->duration();
}

void QGstreamerRecorderControl::record()
{
    if (m_state == QMediaRecorder::RecordingState)
        return;

    m_state = QMediaRecorder::RecordingState;

    if (m_outputLocation.isEmpty()) {
        QString container = m_session->mediaContainerControl()->containerExtension();
        if (container.isEmpty())
            container = "raw";

        m_session->setOutputLocation(QUrl(generateFileName(defaultDir(), container)));
    }

    m_session->dumpGraph("before-record");
    if (!m_hasPreviewState || m_session->state() != QGstreamerCaptureSession::StoppedState) {
        m_session->setState(QGstreamerCaptureSession::RecordingState);
    } else
        emit error(QMediaRecorder::ResourceError, tr("Service has not been started"));

    m_session->dumpGraph("after-record");

    emit stateChanged(m_state);
    updateStatus();
}

void QGstreamerRecorderControl::pause()
{
    if (m_state == QMediaRecorder::PausedState)
        return;

    m_state = QMediaRecorder::PausedState;

    m_session->dumpGraph("before-pause");
    if (!m_hasPreviewState || m_session->state() != QGstreamerCaptureSession::StoppedState) {
        m_session->setState(QGstreamerCaptureSession::PausedState);
    } else
        emit error(QMediaRecorder::ResourceError, tr("Service has not been started"));

    emit stateChanged(m_state);
    updateStatus();
}

void QGstreamerRecorderControl::stop()
{
    if (m_state == QMediaRecorder::StoppedState)
        return;

    m_state = QMediaRecorder::StoppedState;

    if (!m_hasPreviewState) {
        m_session->setState(QGstreamerCaptureSession::StoppedState);
    } else {
        if (m_session->state() != QGstreamerCaptureSession::StoppedState)
            m_session->setState(QGstreamerCaptureSession::PreviewState);
    }

    emit stateChanged(m_state);
    updateStatus();
}

void QGstreamerRecorderControl::applySettings()
{
    //Check the codecs are compatible with container,
    //and choose the compatible codecs/container if omitted
    QGstreamerAudioEncode *audioEncodeControl = m_session->audioEncodeControl();
    QGstreamerVideoEncode *videoEncodeControl = m_session->videoEncodeControl();
    QGstreamerMediaContainerControl *mediaContainerControl = m_session->mediaContainerControl();

    bool needAudio = m_session->captureMode() & QGstreamerCaptureSession::Audio;
    bool needVideo = m_session->captureMode() & QGstreamerCaptureSession::Video;

    QStringList containerCandidates;
    if (mediaContainerControl->containerFormat().isEmpty())
        containerCandidates = mediaContainerControl->supportedContainers();
    else
        containerCandidates << mediaContainerControl->containerFormat();


    QStringList audioCandidates;
    if (needAudio) {
        QAudioEncoderSettings audioSettings = audioEncodeControl->audioSettings();
        if (audioSettings.codec().isEmpty())
            audioCandidates = audioEncodeControl->supportedAudioCodecs();
        else
            audioCandidates << audioSettings.codec();
    }

    QStringList videoCandidates;
    if (needVideo) {
        QVideoEncoderSettings videoSettings = videoEncodeControl->videoSettings();
        if (videoSettings.codec().isEmpty())
            videoCandidates = videoEncodeControl->supportedVideoCodecs();
        else
            videoCandidates << videoSettings.codec();
    }

    QString container;
    QString audioCodec;
    QString videoCodec;

    foreach (const QString &containerCandidate, containerCandidates) {
        QSet<QString> supportedTypes = mediaContainerControl->supportedStreamTypes(containerCandidate);

        audioCodec.clear();
        videoCodec.clear();

        if (needAudio) {
            bool found = false;
            foreach (const QString &audioCandidate, audioCandidates) {
                QSet<QString> audioTypes = audioEncodeControl->supportedStreamTypes(audioCandidate);
                if (!audioTypes.intersect(supportedTypes).isEmpty()) {
                    found = true;
                    audioCodec = audioCandidate;
                    break;
                }
            }
            if (!found)
                continue;
        }

        if (needVideo) {
            bool found = false;
            foreach (const QString &videoCandidate, videoCandidates) {
                QSet<QString> videoTypes = videoEncodeControl->supportedStreamTypes(videoCandidate);
                if (!videoTypes.intersect(supportedTypes).isEmpty()) {
                    found = true;
                    videoCodec = videoCandidate;
                    break;
                }
            }
            if (!found)
                continue;
        }

        container = containerCandidate;
        break;
    }

    if (container.isEmpty()) {
        emit error(QMediaRecorder::FormatError, tr("Not compatible codecs and container format."));
    } else {
        mediaContainerControl->setContainerFormat(container);

        if (needAudio) {
            QAudioEncoderSettings audioSettings = audioEncodeControl->audioSettings();
            audioSettings.setCodec(audioCodec);
            audioEncodeControl->setAudioSettings(audioSettings);
        }

        if (needVideo) {
            QVideoEncoderSettings videoSettings = videoEncodeControl->videoSettings();
            videoSettings.setCodec(videoCodec);
            videoEncodeControl->setVideoSettings(videoSettings);
        }
    }
}


bool QGstreamerRecorderControl::isMuted() const
{
    return m_session->isMuted();
}

void QGstreamerRecorderControl::setMuted(bool muted)
{
    m_session->setMuted(muted);
}

QDir QGstreamerRecorderControl::defaultDir() const
{
    QStringList dirCandidates;

#if defined(Q_WS_MAEMO_6)
    dirCandidates << QLatin1String("/home/user/MyDocs");
#endif

    dirCandidates << QDir::home().filePath("Documents");
    dirCandidates << QDir::home().filePath("My Documents");
    dirCandidates << QDir::homePath();
    dirCandidates << QDir::currentPath();
    dirCandidates << QDir::tempPath();

    foreach (const QString &path, dirCandidates) {
        QDir dir(path);
        if (dir.exists() && QFileInfo(path).isWritable())
            return dir;
    }

    return QDir();
}

QString QGstreamerRecorderControl::generateFileName(const QDir &dir, const QString &ext) const
{

    int lastClip = 0;
    foreach(QString fileName, dir.entryList(QStringList() << QString("clip_*.%1").arg(ext))) {
        int imgNumber = fileName.mid(5, fileName.size()-6-ext.length()).toInt();
        lastClip = qMax(lastClip, imgNumber);
    }

    QString name = QString("clip_%1.%2").arg(lastClip+1,
                                     4, //fieldWidth
                                     10,
                                     QLatin1Char('0')).arg(ext);

    return dir.absoluteFilePath(name);
}

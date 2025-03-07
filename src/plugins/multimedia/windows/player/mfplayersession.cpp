// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "private/qplatformmediaplayer_p.h"

#include <QtCore/qcoreapplication.h>
#include <QtCore/qdatetime.h>
#include <QtCore/qthread.h>
#include <QtCore/qvarlengtharray.h>
#include <QtCore/qdebug.h>
#include <QtCore/qfile.h>
#include <QtCore/qbuffer.h>

#include "private/qplatformaudiooutput_p.h"
#include "qaudiooutput.h"

#include "mfplayercontrol_p.h"
#include "mfevrvideowindowcontrol_p.h"
#include "mfvideorenderercontrol_p.h"
#include <mfmetadata_p.h>
#include <private/qwindowsmfdefs_p.h>
#include <private/qwindowsaudioutils_p.h>

#include "mfplayersession_p.h"
#include <mferror.h>
#include <nserror.h>
#include <winerror.h>
#include "sourceresolver_p.h"
#include "samplegrabber_p.h"
#include "mftvideo_p.h"
#include <wmcodecdsp.h>

#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <Functiondiscoverykeys_devpkey.h>

//#define DEBUG_MEDIAFOUNDATION


MFPlayerSession::MFPlayerSession(MFPlayerControl *playerControl)
    : m_cRef(1)
    , m_playerControl(playerControl)
    , m_session(0)
    , m_presentationClock(0)
    , m_rateControl(0)
    , m_rateSupport(0)
    , m_volumeControl(0)
    , m_netsourceStatistics(0)
    , m_scrubbing(false)
    , m_restoreRate(1)
    , m_sourceResolver(0)
    , m_hCloseEvent(0)
    , m_closing(false)
    , m_mediaTypes(0)
    , m_pendingRate(1)
    , m_status(QMediaPlayer::NoMedia)
    , m_audioSampleGrabber(0)
    , m_audioSampleGrabberNode(0)
    , m_videoProbeMFT(0)

{
    QObject::connect(this, SIGNAL(sessionEvent(IMFMediaEvent*)), this, SLOT(handleSessionEvent(IMFMediaEvent*)));

    m_signalPositionChangeTimer.setInterval(10);
    m_signalPositionChangeTimer.setTimerType(Qt::PreciseTimer);
    m_signalPositionChangeTimer.callOnTimeout(this, &MFPlayerSession::timeout);

    m_pendingState = NoPending;
    ZeroMemory(&m_state, sizeof(m_state));
    m_state.command = CmdStop;
    m_state.prevCmd = CmdNone;
    m_state.rate = 1.0f;
    ZeroMemory(&m_request, sizeof(m_request));
    m_request.command = CmdNone;
    m_request.prevCmd = CmdNone;
    m_request.rate = 1.0f;

    m_audioSampleGrabber = new AudioSampleGrabberCallback;
    m_videoRendererControl = new MFVideoRendererControl;
}

void MFPlayerSession::timeout()
{
    const qint64 pos = position();

    if (pos != m_lastPosition) {
        const bool updatePos = m_timeCounter++ % 10 == 0;
        if (pos >= qint64(m_duration / 10000 - 20)) {
            if (m_playerControl->doLoop()) {
                m_session->Pause();
                setPosition(0);
                positionChanged(0);
            } else {
                if (updatePos)
                    positionChanged(pos);
            }
        } else {
            if (updatePos)
                positionChanged(pos);
        }
        m_lastPosition = pos;
    }
}

void MFPlayerSession::close()
{
#ifdef DEBUG_MEDIAFOUNDATION
    qDebug() << "close";
#endif

    m_signalPositionChangeTimer.stop();
    clear();
    if (!m_session)
        return;

    HRESULT hr = S_OK;
    if (m_session) {
        m_closing = true;
        hr = m_session->Close();
        if (SUCCEEDED(hr)) {
            DWORD dwWaitResult = WaitForSingleObject(m_hCloseEvent, 2000);
            if (dwWaitResult == WAIT_TIMEOUT) {
                qWarning() << "session close time out!";
            }
        }
         m_closing = false;
    }

    if (SUCCEEDED(hr)) {
        if (m_session)
            m_session->Shutdown();
        if (m_sourceResolver)
            m_sourceResolver->shutdown();
    }
    if (m_sourceResolver) {
        m_sourceResolver->Release();
        m_sourceResolver = 0;
    }
    if (m_videoProbeMFT) {
        m_videoProbeMFT->Release();
        m_videoProbeMFT = 0;
    }

    m_videoRendererControl->releaseActivate();
//    } else if (m_playerService->videoWindowControl()) {
//        m_playerService->videoWindowControl()->releaseActivate();
//    }

    if (m_session)
        m_session->Release();
    m_session = 0;
    if (m_hCloseEvent)
        CloseHandle(m_hCloseEvent);
    m_hCloseEvent = 0;
    m_lastPosition = -1;
}

MFPlayerSession::~MFPlayerSession()
{
    m_audioSampleGrabber->Release();
}


void MFPlayerSession::load(const QUrl &url, QIODevice *stream)
{
#ifdef DEBUG_MEDIAFOUNDATION
    qDebug() << "load";
#endif
    clear();

    if (m_status == QMediaPlayer::LoadingMedia && m_sourceResolver)
        m_sourceResolver->cancel();

    if (url.isEmpty() && !stream) {
        close();
        changeStatus(QMediaPlayer::NoMedia);
    } else if (stream && (!stream->isReadable())) {
        close();
        changeStatus(QMediaPlayer::InvalidMedia);
        emit error(QMediaPlayer::ResourceError, tr("Invalid stream source."), true);
    } else if (createSession()) {
        changeStatus(QMediaPlayer::LoadingMedia);
        m_sourceResolver->load(url, stream);
        if (url.isLocalFile())
            m_updateRoutingOnStart = true;
    }
    positionChanged(position());
}

void MFPlayerSession::handleSourceError(long hr)
{
    QString errorString;
    QMediaPlayer::Error errorCode = QMediaPlayer::ResourceError;
    switch (hr) {
    case QMediaPlayer::FormatError:
        errorCode = QMediaPlayer::FormatError;
        errorString = tr("Attempting to play invalid Qt resource.");
        break;
    case NS_E_FILE_NOT_FOUND:
        errorString = tr("The system cannot find the file specified.");
        break;
    case NS_E_SERVER_NOT_FOUND:
        errorString = tr("The specified server could not be found.");
        break;
    case MF_E_UNSUPPORTED_BYTESTREAM_TYPE:
        errorCode = QMediaPlayer::FormatError;
        errorString = tr("Unsupported media type.");
        break;
    case QMM_WININET_E_CANNOT_CONNECT:
        errorCode = QMediaPlayer::NetworkError;
        errorString = tr("Connection to server could not be established.");
        break;
    default:
        qWarning() << "handleSourceError:"
                   << Qt::showbase << Qt::hex << Qt::uppercasedigits << static_cast<quint32>(hr);
        errorString = tr("Failed to load source.");
        break;
    }
    changeStatus(QMediaPlayer::InvalidMedia);
    emit error(errorCode, errorString, true);
}

void MFPlayerSession::handleMediaSourceReady()
{
    if (QMediaPlayer::LoadingMedia != m_status || !m_sourceResolver || m_sourceResolver != sender())
        return;
#ifdef DEBUG_MEDIAFOUNDATION
    qDebug() << "handleMediaSourceReady";
#endif
    HRESULT hr = S_OK;
    IMFMediaSource* mediaSource = m_sourceResolver->mediaSource();

    DWORD dwCharacteristics = 0;
    mediaSource->GetCharacteristics(&dwCharacteristics);
    emit seekableUpdate(MFMEDIASOURCE_CAN_SEEK & dwCharacteristics);

    IMFPresentationDescriptor* sourcePD;
    hr = mediaSource->CreatePresentationDescriptor(&sourcePD);
    if (SUCCEEDED(hr)) {
        m_duration = 0;
        m_metaData = MFMetaData::fromNative(mediaSource);
        emit metaDataChanged();
        sourcePD->GetUINT64(MF_PD_DURATION, &m_duration);
        //convert from 100 nanosecond to milisecond
        emit durationUpdate(qint64(m_duration / 10000));
        setupPlaybackTopology(mediaSource, sourcePD);
        tracksChanged();
        sourcePD->Release();
    } else {
        changeStatus(QMediaPlayer::InvalidMedia);
        emit error(QMediaPlayer::ResourceError, tr("Cannot create presentation descriptor."), true);
    }
}

bool MFPlayerSession::getStreamInfo(IMFStreamDescriptor *stream,
                                    MFPlayerSession::MediaType *type,
                                    QString *name,
                                    QString *language,
                                    GUID *format) const
{
    if (!stream || !type || !name || !language || !format)
        return false;

    *type = Unknown;
    *name = QString();
    *language = QString();

    IMFMediaTypeHandler *typeHandler = nullptr;

    if (SUCCEEDED(stream->GetMediaTypeHandler(&typeHandler))) {

        UINT32 len = 0;
        if (SUCCEEDED(stream->GetStringLength(QMM_MF_SD_STREAM_NAME, &len)) && len > 0) {
            WCHAR *wstr = new WCHAR[len+1];
            if (SUCCEEDED(stream->GetString(QMM_MF_SD_STREAM_NAME, wstr, len+1, &len))) {
                *name = QString::fromUtf16(reinterpret_cast<const char16_t *>(wstr));
            }
            delete []wstr;
        }
        if (SUCCEEDED(stream->GetStringLength(QMM_MF_SD_LANGUAGE, &len)) && len > 0) {
            WCHAR *wstr = new WCHAR[len+1];
            if (SUCCEEDED(stream->GetString(QMM_MF_SD_LANGUAGE, wstr, len+1, &len))) {
                *language = QString::fromUtf16(reinterpret_cast<const char16_t *>(wstr));
            }
            delete []wstr;
        }

        GUID guidMajorType;
        if (SUCCEEDED(typeHandler->GetMajorType(&guidMajorType))) {
            if (guidMajorType == MFMediaType_Audio)
                *type = Audio;
            else if (guidMajorType == MFMediaType_Video)
                *type = Video;
        }

        IMFMediaType *mediaType = nullptr;
        if (SUCCEEDED(typeHandler->GetCurrentMediaType(&mediaType))) {
            mediaType->GetGUID(MF_MT_SUBTYPE, format);
            mediaType->Release();
        }

        typeHandler->Release();
    }

    return *type != Unknown;
}

void MFPlayerSession::setupPlaybackTopology(IMFMediaSource *source, IMFPresentationDescriptor *sourcePD)
{
    HRESULT hr = S_OK;
    // Get the number of streams in the media source.
    DWORD cSourceStreams = 0;
    hr = sourcePD->GetStreamDescriptorCount(&cSourceStreams);
    if (FAILED(hr)) {
        changeStatus(QMediaPlayer::InvalidMedia);
        emit error(QMediaPlayer::ResourceError, tr("Failed to get stream count."), true);
        return;
    }

    IMFTopology *topology;
    hr = MFCreateTopology(&topology);
    if (FAILED(hr)) {
        changeStatus(QMediaPlayer::InvalidMedia);
        emit error(QMediaPlayer::ResourceError, tr("Failed to create topology."), true);
        return;
    }

    // For each stream, create the topology nodes and add them to the topology.
    DWORD succeededCount = 0;
    for (DWORD i = 0; i < cSourceStreams; i++) {
        BOOL selected = FALSE;
        bool streamAdded = false;
        IMFStreamDescriptor *streamDesc = NULL;

        HRESULT hr = sourcePD->GetStreamDescriptorByIndex(i, &selected, &streamDesc);
        if (SUCCEEDED(hr)) {
            // The media might have multiple audio and video streams,
            // only use one of each kind, and only if it is selected by default.
            MediaType mediaType = Unknown;
            QString streamName;
            QString streamLanguage;
            GUID format = GUID_NULL;

            if (getStreamInfo(streamDesc, &mediaType, &streamName, &streamLanguage, &format)) {

                QPlatformMediaPlayer::TrackType trackType = (mediaType == Audio) ?
                            QPlatformMediaPlayer::AudioStream : QPlatformMediaPlayer::VideoStream;

                QLocale::Language lang = streamLanguage.isEmpty() ?
                            QLocale::Language::AnyLanguage : QLocale(streamLanguage).language();

                QMediaMetaData metaData;
                metaData.insert(QMediaMetaData::Title, streamName);
                metaData.insert(QMediaMetaData::Language, lang);

                m_trackInfo[trackType].metaData.append(metaData);
                m_trackInfo[trackType].nativeIndexes.append(i);
                m_trackInfo[trackType].format = format;

                if (((m_mediaTypes & mediaType) == 0) && selected) { // Check if this type isn't already added
                    IMFTopologyNode *sourceNode = addSourceNode(topology, source, sourcePD, streamDesc);
                    if (sourceNode) {
                        IMFTopologyNode *outputNode = addOutputNode(mediaType, topology, 0);
                        if (outputNode) {
                            bool connected = false;
                            if (mediaType == Audio) {
                                if (!m_audioSampleGrabberNode)
                                    connected = setupAudioSampleGrabber(topology, sourceNode, outputNode);
                            }
                            sourceNode->GetTopoNodeID(&m_trackInfo[trackType].sourceNodeId);
                            outputNode->GetTopoNodeID(&m_trackInfo[trackType].outputNodeId);

                            if (!connected)
                                hr = sourceNode->ConnectOutput(0, outputNode, 0);

                            if (FAILED(hr)) {
                                emit error(QMediaPlayer::FormatError, tr("Unable to play any stream."), false);
                            } else {
                                m_trackInfo[trackType].currentIndex = m_trackInfo[trackType].nativeIndexes.count() - 1;
                                streamAdded = true;
                                succeededCount++;
                                m_mediaTypes |= mediaType;
                                switch (mediaType) {
                                case Audio:
                                    emit audioAvailable();
                                    break;
                                case Video:
                                    emit videoAvailable();
                                    break;
                                default:
                                    break;
                                }
                            }
                            outputNode->Release();
                        }
                        sourceNode->Release();
                    }
                }
            }

            if (selected && !streamAdded)
                sourcePD->DeselectStream(i);

            streamDesc->Release();
        }
    }

    if (succeededCount == 0) {
        changeStatus(QMediaPlayer::InvalidMedia);
        emit error(QMediaPlayer::ResourceError, tr("Unable to play."), true);
    } else {
        if (m_trackInfo[QPlatformMediaPlayer::VideoStream].outputNodeId != TOPOID(-1))
            topology = insertMFT(topology, m_trackInfo[QPlatformMediaPlayer::VideoStream].outputNodeId);

        hr = m_session->SetTopology(MFSESSION_SETTOPOLOGY_IMMEDIATE, topology);
        if (SUCCEEDED(hr)) {
            m_updatingTopology = true;
        } else {
            changeStatus(QMediaPlayer::InvalidMedia);
            emit error(QMediaPlayer::ResourceError, tr("Failed to set topology."), true);
        }
    }
    topology->Release();
}

IMFTopologyNode* MFPlayerSession::addSourceNode(IMFTopology* topology, IMFMediaSource* source,
                                                IMFPresentationDescriptor* presentationDesc, IMFStreamDescriptor *streamDesc)
{
    IMFTopologyNode *node = NULL;
    HRESULT hr = MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &node);
    if (SUCCEEDED(hr)) {
        hr = node->SetUnknown(MF_TOPONODE_SOURCE, source);
        if (SUCCEEDED(hr)) {
            hr = node->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, presentationDesc);
            if (SUCCEEDED(hr)) {
                hr = node->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, streamDesc);
                if (SUCCEEDED(hr)) {
                    hr = topology->AddNode(node);
                    if (SUCCEEDED(hr))
                        return node;
                }
            }
        }
        node->Release();
    }
    return NULL;
}

IMFTopologyNode* MFPlayerSession::addOutputNode(MediaType mediaType, IMFTopology* topology, DWORD sinkID)
{
    IMFTopologyNode *node = NULL;
    if (FAILED(MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &node)))
        return NULL;

    IMFActivate *activate = NULL;
    if (mediaType == Audio) {
        if (m_audioOutput) {
            HRESULT hr = MFCreateAudioRendererActivate(&activate);
            if (FAILED(hr)) {
                qWarning() << "Failed to create audio renderer activate";
                node->Release();
                return NULL;
            }

            auto id = m_audioOutput->device.id();
            if (!id.isEmpty()) {
                QString s = QString::fromUtf8(id);
                hr = activate->SetString(MF_AUDIO_RENDERER_ATTRIBUTE_ENDPOINT_ID, (LPCWSTR)s.utf16());
            } else {
                //This is the default one that has been inserted in updateEndpoints(),
                //so give the activate a hint that we want to use the device for multimedia playback
                //then the media foundation will choose an appropriate one.

                //from MSDN:
                //The ERole enumeration defines constants that indicate the role that the system has assigned to an audio endpoint device.
                //eMultimedia: Music, movies, narration, and live music recording.
                hr = activate->SetUINT32(MF_AUDIO_RENDERER_ATTRIBUTE_ENDPOINT_ROLE, eMultimedia);
            }

            if (FAILED(hr)) {
                qWarning() << "Failed to set attribute for audio device" << m_audioOutput->device.description();
                activate->Release();
                node->Release();
                return NULL;
            }
        }
    } else if (mediaType == Video) {
        activate = m_videoRendererControl->createActivate();
    } else {
        // Unknown stream type.
        emit error(QMediaPlayer::FormatError, tr("Unknown stream type."), false);
    }

    if (!activate
            || FAILED(node->SetObject(activate))
            || FAILED(node->SetUINT32(MF_TOPONODE_STREAMID, sinkID))
            || FAILED(node->SetUINT32(MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, FALSE))
            || FAILED(topology->AddNode(node))) {
        node->Release();
        node = NULL;
    }

    if (activate && mediaType == Audio)
        activate->Release();

    return node;
}

bool MFPlayerSession::addAudioSampleGrabberNode(IMFTopology *topology)
{
    HRESULT hr = S_OK;
    IMFMediaType *pType = 0;
    IMFActivate *sinkActivate = 0;
    do {
        hr = MFCreateMediaType(&pType);
        if (FAILED(hr))
            break;

        hr = pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (FAILED(hr))
            break;

        hr = pType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        if (FAILED(hr))
            break;

        hr = MFCreateSampleGrabberSinkActivate(pType, m_audioSampleGrabber, &sinkActivate);
        if (FAILED(hr))
            break;

        // Note: Data is distorted if this attribute is enabled
        hr = sinkActivate->SetUINT32(MF_SAMPLEGRABBERSINK_IGNORE_CLOCK, FALSE);
        if (FAILED(hr))
            break;

        hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &m_audioSampleGrabberNode);
        if (FAILED(hr))
            break;

        hr = m_audioSampleGrabberNode->SetObject(sinkActivate);
        if (FAILED(hr))
            break;

        hr = m_audioSampleGrabberNode->SetUINT32(MF_TOPONODE_STREAMID, 0); // Identifier of the stream sink.
        if (FAILED(hr))
            break;

        hr = m_audioSampleGrabberNode->SetUINT32(MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, FALSE);
        if (FAILED(hr))
            break;

        hr = topology->AddNode(m_audioSampleGrabberNode);
        if (FAILED(hr))
            break;

        pType->Release();
        sinkActivate->Release();
        return true;
    } while (false);

    if (pType)
        pType->Release();
    if (sinkActivate)
        sinkActivate->Release();
    if (m_audioSampleGrabberNode) {
        m_audioSampleGrabberNode->Release();
        m_audioSampleGrabberNode = NULL;
    }
    return false;
}

bool MFPlayerSession::setupAudioSampleGrabber(IMFTopology *topology, IMFTopologyNode *sourceNode, IMFTopologyNode *outputNode)
{
    if (!addAudioSampleGrabberNode(topology))
        return false;

    HRESULT hr = S_OK;
    IMFTopologyNode *pTeeNode = NULL;

    IMFMediaTypeHandler *typeHandler = NULL;
    IMFMediaType *mediaType = NULL;
    do {
        hr = MFCreateTopologyNode(MF_TOPOLOGY_TEE_NODE, &pTeeNode);
        if (FAILED(hr))
            break;
        hr = sourceNode->ConnectOutput(0, pTeeNode, 0);
        if (FAILED(hr))
            break;
        hr = pTeeNode->ConnectOutput(0, outputNode, 0);
        if (FAILED(hr))
            break;
        hr = pTeeNode->ConnectOutput(1, m_audioSampleGrabberNode, 0);
        if (FAILED(hr))
            break;
    } while (false);

    if (pTeeNode)
        pTeeNode->Release();
    if (mediaType)
        mediaType->Release();
    if (typeHandler)
        typeHandler->Release();
    return hr == S_OK;
}

// BindOutputNode
// Sets the IMFStreamSink pointer on an output node.
// IMFActivate pointer in the output node must be converted to an
// IMFStreamSink pointer before the topology loader resolves the topology.
HRESULT BindOutputNode(IMFTopologyNode *pNode)
{
    IUnknown *nodeObject = NULL;
    IMFActivate *activate = NULL;
    IMFStreamSink *stream = NULL;
    IMFMediaSink *sink = NULL;

    HRESULT hr = pNode->GetObject(&nodeObject);
    if (FAILED(hr))
        return hr;

    hr = nodeObject->QueryInterface(IID_PPV_ARGS(&activate));
    if (SUCCEEDED(hr)) {
        DWORD dwStreamID = 0;

        // Try to create the media sink.
        hr = activate->ActivateObject(IID_PPV_ARGS(&sink));
        if (SUCCEEDED(hr))
           dwStreamID = MFGetAttributeUINT32(pNode, MF_TOPONODE_STREAMID, 0);

        if (SUCCEEDED(hr)) {
            // First check if the media sink already has a stream sink with the requested ID.
            hr = sink->GetStreamSinkById(dwStreamID, &stream);
            if (FAILED(hr)) {
                // Create the stream sink.
                hr = sink->AddStreamSink(dwStreamID, NULL, &stream);
            }
        }

        // Replace the node's object pointer with the stream sink.
        if (SUCCEEDED(hr)) {
            hr = pNode->SetObject(stream);
        }
    } else {
        hr = nodeObject->QueryInterface(IID_PPV_ARGS(&stream));
    }

    if (nodeObject)
        nodeObject->Release();
    if (activate)
        activate->Release();
    if (stream)
        stream->Release();
    if (sink)
        sink->Release();
    return hr;
}

// BindOutputNodes
// Sets the IMFStreamSink pointers on all of the output nodes in a topology.
HRESULT BindOutputNodes(IMFTopology *pTopology)
{
    IMFCollection *collection;

    // Get the collection of output nodes.
    HRESULT hr = pTopology->GetOutputNodeCollection(&collection);

    // Enumerate all of the nodes in the collection.
    if (SUCCEEDED(hr)) {
        DWORD cNodes;
        hr = collection->GetElementCount(&cNodes);

        if (SUCCEEDED(hr)) {
            for (DWORD i = 0; i < cNodes; i++) {
                IUnknown *element;
                hr = collection->GetElement(i, &element);
                if (FAILED(hr))
                    break;

                IMFTopologyNode *node;
                hr = element->QueryInterface(IID_IMFTopologyNode, (void**)&node);
                element->Release();
                if (FAILED(hr))
                    break;

                // Bind this node.
                hr = BindOutputNode(node);
                node->Release();
                if (FAILED(hr))
                    break;
            }
        }
        collection->Release();
    }

    return hr;
}

// This method binds output nodes to complete the topology,
// then loads the topology and inserts MFT between the output node
// and a filter connected to the output node.
IMFTopology *MFPlayerSession::insertMFT(IMFTopology *topology, TOPOID outputNodeId)
{
    bool isNewTopology = false;

    IMFTopoLoader *topoLoader = 0;
    IMFTopology *resolvedTopology = 0;
    IMFCollection *outputNodes = 0;

    do {
        if (FAILED(BindOutputNodes(topology)))
            break;

        if (FAILED(MFCreateTopoLoader(&topoLoader)))
            break;

        if (FAILED(topoLoader->Load(topology, &resolvedTopology, NULL))) {
            // Topology could not be resolved, adding ourselves a color converter
            // to the topology might solve the problem
            insertColorConverter(topology, outputNodeId);
            if (FAILED(topoLoader->Load(topology, &resolvedTopology, NULL)))
                break;
        }

        if (insertResizer(resolvedTopology))
            isNewTopology = true;

        // Get all output nodes and search for video output node.
        if (FAILED(resolvedTopology->GetOutputNodeCollection(&outputNodes)))
            break;

        DWORD elementCount = 0;
        if (FAILED(outputNodes->GetElementCount(&elementCount)))
            break;

        for (DWORD n = 0; n < elementCount; n++) {
            IUnknown *element = 0;
            IMFTopologyNode *node = 0;
            IUnknown *outputObject = 0;
            IMFTopologyNode *inputNode = 0;
            IMFTopologyNode *mftNode = 0;
            bool mftAdded = false;

            do {
                if (FAILED(outputNodes->GetElement(n, &element)))
                    break;

                if (FAILED(element->QueryInterface(IID_IMFTopologyNode, (void**)&node)))
                    break;

                TOPOID id;
                if (FAILED(node->GetTopoNodeID(&id)))
                    break;

                if (id != outputNodeId)
                    break;

                if (FAILED(node->GetObject(&outputObject)))
                    break;

                m_videoProbeMFT->setVideoSink(outputObject);

                // Insert MFT between the output node and the node connected to it.
                DWORD outputIndex = 0;
                if (FAILED(node->GetInput(0, &inputNode, &outputIndex)))
                    break;

                if (FAILED(MFCreateTopologyNode(MF_TOPOLOGY_TRANSFORM_NODE, &mftNode)))
                    break;

                if (FAILED(mftNode->SetObject(m_videoProbeMFT)))
                    break;

                if (FAILED(resolvedTopology->AddNode(mftNode)))
                    break;

                if (FAILED(inputNode->ConnectOutput(0, mftNode, 0)))
                    break;

                if (FAILED(mftNode->ConnectOutput(0, node, 0)))
                    break;

                mftAdded = true;
                isNewTopology = true;
            } while (false);

            if (mftNode)
                mftNode->Release();
            if (inputNode)
                inputNode->Release();
            if (node)
                node->Release();
            if (element)
                element->Release();
            if (outputObject)
                outputObject->Release();

            if (mftAdded)
                break;
            else
                m_videoProbeMFT->setVideoSink(NULL);
        }
    } while (false);

    if (outputNodes)
        outputNodes->Release();

    if (topoLoader)
        topoLoader->Release();

    if (isNewTopology) {
        topology->Release();
        return resolvedTopology;
    }

    if (resolvedTopology)
        resolvedTopology->Release();

    return topology;
}

// This method checks if the topology contains a color converter transform (CColorConvertDMO),
// if it does it inserts a resizer transform (CResizerDMO) to handle dynamic frame size change
// of the video stream.
// Returns true if it inserted a resizer
bool MFPlayerSession::insertResizer(IMFTopology *topology)
{
    bool inserted = false;
    WORD elementCount = 0;
    IMFTopologyNode *node = 0;
    IUnknown *object = 0;
    IWMColorConvProps *colorConv = 0;
    IMFTransform *resizer = 0;
    IMFTopologyNode *resizerNode = 0;
    IMFTopologyNode *inputNode = 0;

    HRESULT hr = topology->GetNodeCount(&elementCount);
    if (FAILED(hr))
        return false;

    for (WORD i = 0; i < elementCount; ++i) {
        if (node) {
            node->Release();
            node = 0;
        }
        if (object) {
            object->Release();
            object = 0;
        }

        if (FAILED(topology->GetNode(i, &node)))
            break;

        MF_TOPOLOGY_TYPE nodeType;
        if (FAILED(node->GetNodeType(&nodeType)))
            break;

        if (nodeType != MF_TOPOLOGY_TRANSFORM_NODE)
            continue;

        if (FAILED(node->GetObject(&object)))
            break;

        if (FAILED(object->QueryInterface(&colorConv)))
            continue;

        if (FAILED(CoCreateInstance(CLSID_CResizerDMO, NULL, CLSCTX_INPROC_SERVER, IID_IMFTransform, (void**)&resizer)))
            break;

        if (FAILED(MFCreateTopologyNode(MF_TOPOLOGY_TRANSFORM_NODE, &resizerNode)))
            break;

        if (FAILED(resizerNode->SetObject(resizer)))
            break;

        if (FAILED(topology->AddNode(resizerNode)))
            break;

        DWORD outputIndex = 0;
        if (FAILED(node->GetInput(0, &inputNode, &outputIndex))) {
            topology->RemoveNode(resizerNode);
            break;
        }

        if (FAILED(inputNode->ConnectOutput(0, resizerNode, 0))) {
            topology->RemoveNode(resizerNode);
            break;
        }

        if (FAILED(resizerNode->ConnectOutput(0, node, 0))) {
            inputNode->ConnectOutput(0, node, 0);
            topology->RemoveNode(resizerNode);
            break;
        }

        inserted = true;
        break;
    }

    if (node)
        node->Release();
    if (object)
        object->Release();
    if (colorConv)
        colorConv->Release();
    if (resizer)
        resizer->Release();
    if (resizerNode)
        resizerNode->Release();
    if (inputNode)
        inputNode->Release();

    return inserted;
}

// This method inserts a color converter (CColorConvertDMO) in the topology,
// typically to convert to RGB format.
// Usually this converter is automatically inserted when the topology is resolved but
// for some reason it fails to do so in some cases, we then do it ourselves.
void MFPlayerSession::insertColorConverter(IMFTopology *topology, TOPOID outputNodeId)
{
    IMFCollection *outputNodes = 0;

    if (FAILED(topology->GetOutputNodeCollection(&outputNodes)))
        return;

    DWORD elementCount = 0;
    if (FAILED(outputNodes->GetElementCount(&elementCount)))
        goto done;

    for (DWORD n = 0; n < elementCount; n++) {
        IUnknown *element = 0;
        IMFTopologyNode *node = 0;
        IMFTopologyNode *inputNode = 0;
        IMFTopologyNode *mftNode = 0;
        IMFTransform *converter = 0;

        do {
            if (FAILED(outputNodes->GetElement(n, &element)))
                break;

            if (FAILED(element->QueryInterface(IID_IMFTopologyNode, (void**)&node)))
                break;

            TOPOID id;
            if (FAILED(node->GetTopoNodeID(&id)))
                break;

            if (id != outputNodeId)
                break;

            DWORD outputIndex = 0;
            if (FAILED(node->GetInput(0, &inputNode, &outputIndex)))
                break;

            if (FAILED(MFCreateTopologyNode(MF_TOPOLOGY_TRANSFORM_NODE, &mftNode)))
                break;

            if (FAILED(CoCreateInstance(CLSID_CColorConvertDMO, NULL, CLSCTX_INPROC_SERVER, IID_IMFTransform, (void**)&converter)))
                break;

            if (FAILED(mftNode->SetObject(converter)))
                break;

            if (FAILED(topology->AddNode(mftNode)))
                break;

            if (FAILED(inputNode->ConnectOutput(0, mftNode, 0)))
                break;

            if (FAILED(mftNode->ConnectOutput(0, node, 0)))
                break;

        } while (false);

        if (mftNode)
            mftNode->Release();
        if (inputNode)
            inputNode->Release();
        if (node)
            node->Release();
        if (element)
            element->Release();
        if (converter)
            converter->Release();
    }

done:
    if (outputNodes)
        outputNodes->Release();
}

void MFPlayerSession::stop(bool immediate)
{
#ifdef DEBUG_MEDIAFOUNDATION
    qDebug() << "stop";
#endif
    if (!immediate && m_pendingState != NoPending) {
        m_request.setCommand(CmdStop);
    } else {
        if (m_state.command == CmdStop)
            return;

        if (m_scrubbing)
            scrub(false);

        if (SUCCEEDED(m_session->Stop())) {

            m_state.setCommand(CmdStop);
            m_pendingState = CmdPending;
            if (m_status != QMediaPlayer::EndOfMedia) {
                m_position = 0;
                positionChanged(0);
            }
        } else {
            emit error(QMediaPlayer::ResourceError, tr("Failed to stop."), true);
        }
    }
}

void MFPlayerSession::start()
{
    if (status() == QMediaPlayer::LoadedMedia && m_updateRoutingOnStart) {
        m_updateRoutingOnStart = false;
        updateOutputRouting();
    }

    if (m_status == QMediaPlayer::EndOfMedia) {
        m_position = 0; // restart from the beginning
        positionChanged(0);
    }

#ifdef DEBUG_MEDIAFOUNDATION
    qDebug() << "start";
#endif

    if (m_pendingState != NoPending) {
        m_request.setCommand(CmdStart);
    } else {
        if (m_state.command == CmdStart)
            return;

        if (m_scrubbing) {
            scrub(false);
            m_position = position() * 10000;
        }

        if (m_restorePosition >= 0) {
            m_position = m_restorePosition;
            if (!m_updatingTopology)
                m_restorePosition = -1;
        }

        PROPVARIANT varStart;
        InitPropVariantFromInt64(m_position, &varStart);

        if (SUCCEEDED(m_session->Start(&GUID_NULL, &varStart))) {
            m_state.setCommand(CmdStart);
            m_pendingState = CmdPending;
            m_lastSeekPos = m_position;
            m_lastSeekSysTime = MFGetSystemTime();
            m_altTiming = false;
        } else {
            emit error(QMediaPlayer::ResourceError, tr("failed to start playback"), true);
        }
        PropVariantClear(&varStart);
    }
}

void MFPlayerSession::pause()
{
#ifdef DEBUG_MEDIAFOUNDATION
    qDebug() << "pause";
#endif
    if (m_pendingState != NoPending) {
        m_request.setCommand(CmdPause);
    } else {
        if (m_state.command == CmdPause)
            return;

        if (SUCCEEDED(m_session->Pause())) {
            m_state.setCommand(CmdPause);
            m_pendingState = CmdPending;
        } else {
            emit error(QMediaPlayer::ResourceError, tr("Failed to pause."), false);
        }
        if (m_status == QMediaPlayer::EndOfMedia) {
            setPosition(0);
            positionChanged(0);
        }
    }
}

void MFPlayerSession::changeStatus(QMediaPlayer::MediaStatus newStatus)
{
    if (m_status == newStatus)
        return;
#ifdef DEBUG_MEDIAFOUNDATION
    qDebug() << "MFPlayerSession::changeStatus" << newStatus;
#endif
    m_status = newStatus;
    emit statusChanged();
}

QMediaPlayer::MediaStatus MFPlayerSession::status() const
{
    return m_status;
}

bool MFPlayerSession::createSession()
{
    close();

    Q_ASSERT(m_session == NULL);

    HRESULT hr = MFCreateMediaSession(NULL, &m_session);
    if (FAILED(hr)) {
        changeStatus(QMediaPlayer::InvalidMedia);
        emit error(QMediaPlayer::ResourceError, tr("Unable to create mediasession."), true);
        return false;
    }

    m_hCloseEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    hr = m_session->BeginGetEvent(this, m_session);
    if (FAILED(hr)) {
        changeStatus(QMediaPlayer::InvalidMedia);
        emit error(QMediaPlayer::ResourceError, tr("Unable to pull session events."), false);
        close();
        return false;
    }

    m_sourceResolver = new SourceResolver();
    QObject::connect(m_sourceResolver, &SourceResolver::mediaSourceReady, this, &MFPlayerSession::handleMediaSourceReady);
    QObject::connect(m_sourceResolver, &SourceResolver::error, this, &MFPlayerSession::handleSourceError);

    m_videoProbeMFT = new MFTransform;
//    for (int i = 0; i < m_videoProbes.size(); ++i)
//        m_videoProbeMFT->addProbe(m_videoProbes.at(i));

    m_position = 0;
    return true;
}

qint64 MFPlayerSession::position()
{
    if (m_request.command == CmdSeek || m_request.command == CmdSeekResume)
        return m_request.start;

    if (m_pendingState == SeekPending)
        return m_state.start;

    if (m_state.command == CmdStop)
        return m_position / 10000;

    if (m_presentationClock) {
        MFTIME time, sysTime;
        if (FAILED(m_presentationClock->GetCorrelatedTime(0, &time, &sysTime)))
            return m_position / 10000;

        if (m_state.rate > 0) {
            if (time > 0 && qint64(time) < m_lastSeekPos)
                m_altTiming = true;

            if (m_altTiming)
                return (m_lastSeekPos + MFGetSystemTime() - m_lastSeekSysTime) / 10000;
        }

        return qint64(time / 10000);
    }
    return m_position / 10000;
}

void MFPlayerSession::setPosition(qint64 position)
{
#ifdef DEBUG_MEDIAFOUNDATION
    qDebug() << "setPosition";
#endif
    if (m_pendingState != NoPending) {
        m_request.setCommand(CmdSeek);
        m_request.start = position;
    } else {
        setPositionInternal(position, CmdNone);
    }
}

void MFPlayerSession::setPositionInternal(qint64 position, Command requestCmd)
{
    if (m_status == QMediaPlayer::EndOfMedia)
        changeStatus(QMediaPlayer::LoadedMedia);
    if (m_state.command == CmdStop && requestCmd != CmdSeekResume) {
        m_position = position * 10000;
        // Even though the position is not actually set on the session yet,
        // report it to have changed anyway for UI controls to be updated
        positionChanged(this->position());
        return;
    }

    if (m_state.command == CmdPause)
        scrub(true);

#ifdef DEBUG_MEDIAFOUNDATION
    qDebug() << "setPositionInternal";
#endif

    PROPVARIANT varStart;
    varStart.vt = VT_I8;
    varStart.hVal.QuadPart = LONGLONG(position * 10000);
    if (SUCCEEDED(m_session->Start(NULL, &varStart)))
    {
        m_lastSeekPos = position * 10000;
        m_lastSeekSysTime = MFGetSystemTime();
        m_altTiming = false;

        PropVariantClear(&varStart);
        // Store the pending state.
        m_state.setCommand(CmdStart);
        m_state.start = position;
        m_pendingState = SeekPending;
    } else {
        emit error(QMediaPlayer::ResourceError, tr("Failed to seek."), true);
    }
}

qreal MFPlayerSession::playbackRate() const
{
    if (m_scrubbing)
        return m_restoreRate;
    return m_state.rate;
}

void MFPlayerSession::setPlaybackRate(qreal rate)
{
    if (m_scrubbing) {
        m_restoreRate = rate;
        emit playbackRateChanged(rate);
        return;
    }
    setPlaybackRateInternal(rate);
}

void MFPlayerSession::setPlaybackRateInternal(qreal rate)
{
    if (rate == m_request.rate)
        return;

    m_pendingRate = rate;
    if (!m_rateSupport)
        return;

#ifdef DEBUG_MEDIAFOUNDATION
    qDebug() << "setPlaybackRate";
#endif
    BOOL isThin = FALSE;

    //from MSDN http://msdn.microsoft.com/en-us/library/aa965220%28v=vs.85%29.aspx
    //Thinning applies primarily to video streams.
    //In thinned mode, the source drops delta frames and deliver only key frames.
    //At very high playback rates, the source might skip some key frames (for example, deliver every other key frame).

    if (FAILED(m_rateSupport->IsRateSupported(FALSE, rate, NULL))) {
        isThin = TRUE;
        if (FAILED(m_rateSupport->IsRateSupported(isThin, rate, NULL))) {
            qWarning() << "unable to set playbackrate = " << rate;
            m_pendingRate = m_request.rate = m_state.rate;
            return;
        }
    }
    if (m_pendingState != NoPending) {
        m_request.rate = rate;
        m_request.isThin = isThin;
        // Remember the current transport state (play, paused, etc), so that we
        // can restore it after the rate change, if necessary. However, if
        // anothercommand is already pending, that one takes precedent.
        if (m_request.command == CmdNone)
            m_request.setCommand(m_state.command);
    } else {
        //No pending operation. Commit the new rate.
        commitRateChange(rate, isThin);
    }
}

void MFPlayerSession::commitRateChange(qreal rate, BOOL isThin)
{
#ifdef DEBUG_MEDIAFOUNDATION
    qDebug() << "commitRateChange";
#endif
    Q_ASSERT(m_pendingState == NoPending);
    MFTIME  hnsSystemTime = 0;
    MFTIME  hnsClockTime = 0;
    Command cmdNow = m_state.command;
    bool resetPosition = false;
    // Allowed rate transitions:
    // Positive <-> negative:   Stopped
    // Negative <-> zero:       Stopped
    // Postive <-> zero:        Paused or stopped
    if ((rate > 0 && m_state.rate <= 0) || (rate < 0 && m_state.rate >= 0)) {
        if (cmdNow == CmdStart) {
            // Get the current clock position. This will be the restart time.
            m_presentationClock->GetCorrelatedTime(0, &hnsClockTime, &hnsSystemTime);
            Q_ASSERT(hnsSystemTime != 0);

            if (rate < 0 || m_state.rate < 0)
                m_request.setCommand(CmdSeekResume);
            else if (isThin || m_state.isThin)
                m_request.setCommand(CmdStartAndSeek);
            else
                m_request.setCommand(CmdStart);

            // We need to stop only when dealing with negative rates
            if (rate >= 0 && m_state.rate >= 0)
                pause();
            else
                stop();

            // If we deal with negative rates, we stopped the session and consequently
            // reset the position to zero. We then need to resume to the current position.
            m_request.start = hnsClockTime / 10000;
        } else if (cmdNow == CmdPause) {
            if (rate < 0 || m_state.rate < 0) {
                // The current state is paused.
                // For this rate change, the session must be stopped. However, the
                // session cannot transition back from stopped to paused.
                // Therefore, this rate transition is not supported while paused.
                qWarning() << "Unable to change rate from positive to negative or vice versa in paused state";
                rate = m_state.rate;
                isThin = m_state.isThin;
                goto done;
            }

            // This happens when resuming playback after scrubbing in pause mode.
            // This transition requires the session to be paused. Even though our
            // internal state is set to paused, the session might not be so we need
            // to enforce it
            if (rate > 0 && m_state.rate == 0) {
                m_state.setCommand(CmdNone);
                pause();
            }
        }
    } else if (rate == 0 && m_state.rate > 0) {
        if (cmdNow != CmdPause) {
            // Transition to paused.
            // This transisition requires the paused state.
            // Pause and set the rate.
            pause();

            // Request: Switch back to current state.
            m_request.setCommand(cmdNow);
        }
    } else if (rate == 0 && m_state.rate < 0) {
        // Changing rate from negative to zero requires to stop the session
        m_presentationClock->GetCorrelatedTime(0, &hnsClockTime, &hnsSystemTime);

        m_request.setCommand(CmdSeekResume);

        stop();

        // Resume to the current position (stop() will reset the position to 0)
        m_request.start = hnsClockTime / 10000;
    } else if (!isThin && m_state.isThin) {
        if (cmdNow == CmdStart) {
            // When thinning, only key frames are read and presented. Going back
            // to normal playback requires to reset the current position to force
            // the pipeline to decode the actual frame at the current position
            // (which might be earlier than the last decoded key frame)
            resetPosition = true;
        } else if (cmdNow == CmdPause) {
            // If paused, don't reset the position until we resume, otherwise
            // a new frame will be rendered
            m_presentationClock->GetCorrelatedTime(0, &hnsClockTime, &hnsSystemTime);
            m_request.setCommand(CmdSeekResume);
            m_request.start = hnsClockTime / 10000;
        }

    }

    // Set the rate.
    if (FAILED(m_rateControl->SetRate(isThin, rate))) {
        qWarning() << "failed to set playbackrate = " << rate;
        rate = m_state.rate;
        isThin = m_state.isThin;
        goto done;
    }

    if (resetPosition) {
        m_presentationClock->GetCorrelatedTime(0, &hnsClockTime, &hnsSystemTime);
        setPosition(hnsClockTime / 10000);
    }

done:
    // Adjust our current rate and requested rate.
    m_pendingRate = m_request.rate = m_state.rate = rate;
    if (rate != 0)
        m_state.isThin = isThin;
    emit playbackRateChanged(rate);
}

void MFPlayerSession::scrub(bool enableScrub)
{
    if (m_scrubbing == enableScrub)
        return;

    m_scrubbing = enableScrub;

    if (!canScrub()) {
        if (!enableScrub)
            m_pendingRate = m_restoreRate;
        return;
    }

    if (enableScrub)  {
        // Enter scrubbing mode. Cache the rate.
        m_restoreRate = m_request.rate;
        setPlaybackRateInternal(0.0f);
    } else {
        // Leaving scrubbing mode. Restore the old rate.
        setPlaybackRateInternal(m_restoreRate);
    }
}

void MFPlayerSession::setVolume(float volume)
{
    if (m_volume == volume)
        return;
    m_volume = volume;

    if (!m_muted)
        setVolumeInternal(volume);
}

void MFPlayerSession::setMuted(bool muted)
{
    if (m_muted == muted)
        return;
    m_muted = muted;

    setVolumeInternal(muted ? 0 : m_volume);
}

void MFPlayerSession::setVolumeInternal(float volume)
{
    if (m_volumeControl) {
        quint32 channelCount = 0;
        if (!SUCCEEDED(m_volumeControl->GetChannelCount(&channelCount))
                || channelCount == 0)
            return;

        for (quint32 i = 0; i < channelCount; ++i)
            m_volumeControl->SetChannelVolume(i, volume);
    }
}

float MFPlayerSession::bufferProgress()
{
    if (!m_netsourceStatistics)
        return 0;
    PROPVARIANT var;
    PropVariantInit(&var);
    PROPERTYKEY key;
    key.fmtid = MFNETSOURCE_STATISTICS;
    key.pid = MFNETSOURCE_BUFFERPROGRESS_ID;
    int progress = -1;
    // GetValue returns S_FALSE if the property is not available, which has
    // a value > 0. We therefore can't use the SUCCEEDED macro here.
    if (m_netsourceStatistics->GetValue(key, &var) == S_OK) {
        progress = var.lVal;
        PropVariantClear(&var);
    }

#ifdef DEBUG_MEDIAFOUNDATION
    qDebug() << "bufferProgress: progress = " << progress;
#endif

    return progress/100.;
}

QMediaTimeRange MFPlayerSession::availablePlaybackRanges()
{
    // defaults to the whole media
    qint64 start = 0;
    qint64 end = qint64(m_duration / 10000);

    if (m_netsourceStatistics) {
        PROPVARIANT var;
        PropVariantInit(&var);
        PROPERTYKEY key;
        key.fmtid = MFNETSOURCE_STATISTICS;
        key.pid = MFNETSOURCE_SEEKRANGESTART_ID;
        // GetValue returns S_FALSE if the property is not available, which has
        // a value > 0. We therefore can't use the SUCCEEDED macro here.
        if (m_netsourceStatistics->GetValue(key, &var) == S_OK) {
            start = qint64(var.uhVal.QuadPart / 10000);
            PropVariantClear(&var);
            PropVariantInit(&var);
            key.pid = MFNETSOURCE_SEEKRANGEEND_ID;
            if (m_netsourceStatistics->GetValue(key, &var) == S_OK) {
                end = qint64(var.uhVal.QuadPart / 10000);
                PropVariantClear(&var);
            }
        }
    }

    return QMediaTimeRange(start, end);
}

HRESULT MFPlayerSession::QueryInterface(REFIID riid, void** ppvObject)
{
    if (!ppvObject)
        return E_POINTER;
    if (riid == IID_IMFAsyncCallback) {
        *ppvObject = static_cast<IMFAsyncCallback*>(this);
    } else if (riid == IID_IUnknown) {
        *ppvObject = static_cast<IUnknown*>(this);
    } else {
        *ppvObject =  NULL;
        return E_NOINTERFACE;
    }
    return S_OK;
}

ULONG MFPlayerSession::AddRef(void)
{
    return InterlockedIncrement(&m_cRef);
}

ULONG MFPlayerSession::Release(void)
{
    LONG cRef = InterlockedDecrement(&m_cRef);
    if (cRef == 0)
        this->deleteLater();
    return cRef;
}

HRESULT MFPlayerSession::Invoke(IMFAsyncResult *pResult)
{
    if (pResult->GetStateNoAddRef() != m_session)
        return S_OK;

    IMFMediaEvent *pEvent = NULL;
    // Get the event from the event queue.
    HRESULT hr = m_session->EndGetEvent(pResult, &pEvent);
    if (FAILED(hr)) {
        return S_OK;
    }

    MediaEventType meType = MEUnknown;
    hr = pEvent->GetType(&meType);
    if (FAILED(hr)) {
        pEvent->Release();
        return S_OK;
    }

    if (meType == MESessionClosed) {
        SetEvent(m_hCloseEvent);
        pEvent->Release();
        return S_OK;
    } else {
        hr = m_session->BeginGetEvent(this, m_session);
        if (FAILED(hr)) {
            pEvent->Release();
            return S_OK;
        }
    }

    if (!m_closing) {
        emit sessionEvent(pEvent);
    } else {
        pEvent->Release();
    }
    return S_OK;
}

void MFPlayerSession::handleSessionEvent(IMFMediaEvent *sessionEvent)
{
    HRESULT hrStatus = S_OK;
    HRESULT hr = sessionEvent->GetStatus(&hrStatus);
    if (FAILED(hr) || !m_session) {
        sessionEvent->Release();
        return;
    }

    MediaEventType meType = MEUnknown;
    hr = sessionEvent->GetType(&meType);
#ifdef DEBUG_MEDIAFOUNDATION
    if (FAILED(hrStatus))
        qDebug() << "handleSessionEvent: MediaEventType = " << meType << "Failed";
    else
        qDebug() << "handleSessionEvent: MediaEventType = " << meType;
#endif

    switch (meType) {
    case MENonFatalError: {
            PROPVARIANT var;
            PropVariantInit(&var);
            sessionEvent->GetValue(&var);
            qWarning() << "handleSessionEvent: non fatal error = " << var.ulVal;
            PropVariantClear(&var);
            emit error(QMediaPlayer::ResourceError, tr("Media session non-fatal error."), false);
        }
        break;
    case MESourceUnknown:
        changeStatus(QMediaPlayer::InvalidMedia);
        break;
    case MEError:
        if (hrStatus == MF_E_ALREADY_INITIALIZED) {
            // Workaround for a possible WMF issue that causes an error
            // with some specific videos, which play fine otherwise.
#ifdef DEBUG_MEDIAFOUNDATION
            qDebug() << "handleSessionEvent: ignoring MF_E_ALREADY_INITIALIZED";
#endif
            break;
        }
        changeStatus(QMediaPlayer::InvalidMedia);
        qWarning() << "handleSessionEvent: serious error = "
                   << Qt::showbase << Qt::hex << Qt::uppercasedigits << static_cast<quint32>(hrStatus);
        switch (hrStatus) {
        case MF_E_NET_READ:
            emit error(QMediaPlayer::NetworkError, tr("Error reading from the network."), true);
            break;
        case MF_E_NET_WRITE:
            emit error(QMediaPlayer::NetworkError, tr("Error writing to the network."), true);
            break;
        case NS_E_FIREWALL:
            emit error(QMediaPlayer::NetworkError, tr("Network packets might be blocked by a firewall."), true);
            break;
        case MF_E_MEDIAPROC_WRONGSTATE:
            emit error(QMediaPlayer::ResourceError, tr("Media session state error."), true);
            break;
        default:
            emit error(QMediaPlayer::ResourceError, tr("Media session serious error."), true);
            break;
        }
        break;
    case MESessionRateChanged:
        // If the rate change succeeded, we've already got the rate
        // cached. If it failed, try to get the actual rate.
        if (FAILED(hrStatus)) {
            PROPVARIANT var;
            PropVariantInit(&var);
            if (SUCCEEDED(sessionEvent->GetValue(&var)) && (var.vt == VT_R4))    {
                m_state.rate = var.fltVal;
            }
            emit playbackRateChanged(playbackRate());
        }
        break;
    case MESessionScrubSampleComplete :
        if (m_scrubbing)
            updatePendingCommands(CmdStart);
        break;
    case MESessionStarted:
        if (m_status == QMediaPlayer::EndOfMedia
                || m_status == QMediaPlayer::LoadedMedia) {
            // If the session started, then enough data is buffered to play
            changeStatus(QMediaPlayer::BufferedMedia);
        }

        updatePendingCommands(CmdStart);
        // playback started, we can now set again the procAmpValues if they have been
        // changed previously (these are lost when loading a new media)
//        if (m_playerService->videoWindowControl()) {
//            m_playerService->videoWindowControl()->applyImageControls();
//        }
        m_signalPositionChangeTimer.start();
        break;
    case MESessionStopped:
        if (m_status != QMediaPlayer::EndOfMedia) {
            m_position = 0;

            // Reset to Loaded status unless we are loading a new media
            // or changing the playback rate to negative values (stop required)
            if (m_status != QMediaPlayer::LoadingMedia && m_request.command != CmdSeekResume)
                changeStatus(QMediaPlayer::LoadedMedia);
        }
        updatePendingCommands(CmdStop);
        m_signalPositionChangeTimer.stop();
        break;
    case MESessionPaused:
        m_position = position() * 10000;
        updatePendingCommands(CmdPause);
        m_signalPositionChangeTimer.stop();
        if (m_status == QMediaPlayer::LoadedMedia)
            setPosition(position());
        break;
    case MEReconnectStart:
#ifdef DEBUG_MEDIAFOUNDATION
            qDebug() << "MEReconnectStart" << ((hrStatus == S_OK) ? "OK" : "Failed");
#endif
        break;
    case MEReconnectEnd:
#ifdef DEBUG_MEDIAFOUNDATION
            qDebug() << "MEReconnectEnd" << ((hrStatus == S_OK) ? "OK" : "Failed");
#endif
        break;
    case MESessionTopologySet:
        if (FAILED(hrStatus)) {
            changeStatus(QMediaPlayer::InvalidMedia);
            emit error(QMediaPlayer::FormatError, tr("Unsupported media, a codec is missing."), true);
        } else {
            if (m_audioSampleGrabberNode) {
                IUnknown *obj = 0;
                if (SUCCEEDED(m_audioSampleGrabberNode->GetObject(&obj))) {
                    IMFStreamSink *streamSink = 0;
                    if (SUCCEEDED(obj->QueryInterface(IID_PPV_ARGS(&streamSink)))) {
                        IMFMediaTypeHandler *typeHandler = 0;
                        if (SUCCEEDED(streamSink->GetMediaTypeHandler((&typeHandler)))) {
                            IMFMediaType *mediaType = 0;
                            if (SUCCEEDED(typeHandler->GetCurrentMediaType(&mediaType))) {
                                m_audioSampleGrabber->setFormat(QWindowsAudioUtils::mediaTypeToFormat(mediaType));
                                mediaType->Release();
                            }
                            typeHandler->Release();
                        }
                        streamSink->Release();
                    }
                    obj->Release();
                }
            }

            // Topology is resolved and successfuly set, this happens only after loading a new media.
            // Make sure we always start the media from the beginning
            m_lastPosition = -1;
            m_position = 0;
            positionChanged(0);
            changeStatus(QMediaPlayer::LoadedMedia);
        }
        break;
    }

    if (FAILED(hrStatus)) {
        sessionEvent->Release();
        return;
    }

    switch (meType) {
    case MEBufferingStarted:
        changeStatus(QMediaPlayer::StalledMedia);
        emit bufferProgressChanged(bufferProgress());
        break;
    case MEBufferingStopped:
        changeStatus(QMediaPlayer::BufferedMedia);
        emit bufferProgressChanged(bufferProgress());
        break;
    case MESessionEnded:
        m_pendingState = NoPending;
        m_state.command = CmdStop;
        m_state.prevCmd = CmdNone;
        m_request.command = CmdNone;
        m_request.prevCmd = CmdNone;

        //keep reporting the final position after end of media
        m_position = qint64(m_duration);
        positionChanged(position());

        changeStatus(QMediaPlayer::EndOfMedia);
        break;
    case MEEndOfPresentationSegment:
        break;
    case MESessionTopologyStatus: {
            UINT32 status;
            if (SUCCEEDED(sessionEvent->GetUINT32(MF_EVENT_TOPOLOGY_STATUS, &status))) {
                if (status == MF_TOPOSTATUS_READY) {
                    IMFClock* clock;
                    if (SUCCEEDED(m_session->GetClock(&clock))) {
                        clock->QueryInterface(IID_IMFPresentationClock, (void**)(&m_presentationClock));
                        clock->Release();
                    }

                    if (SUCCEEDED(MFGetService(m_session, MF_RATE_CONTROL_SERVICE, IID_PPV_ARGS(&m_rateControl)))) {
                        if (SUCCEEDED(MFGetService(m_session, MF_RATE_CONTROL_SERVICE, IID_PPV_ARGS(&m_rateSupport)))) {
                            if (SUCCEEDED(m_rateSupport->IsRateSupported(TRUE, 0, NULL)))
                                m_canScrub = true;
                        }
                        BOOL isThin = FALSE;
                        float rate = 1;
                        if (SUCCEEDED(m_rateControl->GetRate(&isThin, &rate))) {
                            if (m_pendingRate != rate) {
                                m_state.rate = m_request.rate = rate;
                                setPlaybackRate(m_pendingRate);
                            }
                        }
                    }
                    MFGetService(m_session, MFNETSOURCE_STATISTICS_SERVICE, IID_PPV_ARGS(&m_netsourceStatistics));

                    if (SUCCEEDED(MFGetService(m_session, MR_STREAM_VOLUME_SERVICE, IID_PPV_ARGS(&m_volumeControl))))
                        setVolumeInternal(m_muted ? 0 : m_volume);

                    m_updatingTopology = false;
                    stop();
                }
            }
        }
        break;
    default:
        break;
    }

    sessionEvent->Release();
}

void MFPlayerSession::updatePendingCommands(Command command)
{
    positionChanged(position());
    if (m_state.command != command || m_pendingState == NoPending)
        return;

    // Seek while paused completed
    if (m_pendingState == SeekPending && m_state.prevCmd == CmdPause) {
        m_pendingState = NoPending;
        // A seek operation actually restarts playback. If scrubbing is possible, playback rate
        // is set to 0.0 at this point and we just need to reset the current state to Pause.
        // If scrubbing is not possible, the playback rate was not changed and we explicitly need
        // to re-pause playback.
        if (!canScrub())
            pause();
        else
            m_state.setCommand(CmdPause);
    }

    m_pendingState = NoPending;

    //First look for rate changes.
    if (m_request.rate != m_state.rate) {
        commitRateChange(m_request.rate, m_request.isThin);
    }

    // Now look for new requests.
    if (m_pendingState == NoPending) {
        switch (m_request.command) {
        case CmdStart:
            start();
            break;
        case CmdPause:
            pause();
            break;
        case CmdStop:
            stop();
            break;
        case CmdSeek:
        case CmdSeekResume:
            setPositionInternal(m_request.start, m_request.command);
            break;
        case CmdStartAndSeek:
            start();
            setPositionInternal(m_request.start, m_request.command);
            break;
        default:
            break;
        }
        m_request.setCommand(CmdNone);
    }

}

bool MFPlayerSession::canScrub() const
{
    return m_canScrub && m_rateSupport && m_rateControl;
}

void MFPlayerSession::clear()
{
#ifdef DEBUG_MEDIAFOUNDATION
    qDebug() << "MFPlayerSession::clear";
#endif
    m_mediaTypes = 0;
    m_canScrub = false;

    m_pendingState = NoPending;
    m_state.command = CmdStop;
    m_state.prevCmd = CmdNone;
    m_request.command = CmdNone;
    m_request.prevCmd = CmdNone;

    for (int i = 0; i < QPlatformMediaPlayer::NTrackTypes; ++i) {
        m_trackInfo[i].metaData.clear();
        m_trackInfo[i].nativeIndexes.clear();
        m_trackInfo[i].currentIndex = -1;
        m_trackInfo[i].sourceNodeId = TOPOID(-1);
        m_trackInfo[i].outputNodeId = TOPOID(-1);
        m_trackInfo[i].format = GUID_NULL;
    }

    if (!m_metaData.isEmpty()) {
        m_metaData.clear();
        emit metaDataChanged();
    }

    if (m_presentationClock) {
        m_presentationClock->Release();
        m_presentationClock = NULL;
    }
    if (m_rateControl) {
        m_rateControl->Release();
        m_rateControl = NULL;
    }
    if (m_rateSupport) {
        m_rateSupport->Release();
        m_rateSupport = NULL;
    }
    if (m_volumeControl) {
        m_volumeControl->Release();
        m_volumeControl = NULL;
    }
    if (m_netsourceStatistics) {
        m_netsourceStatistics->Release();
        m_netsourceStatistics = NULL;
    }
    if (m_audioSampleGrabberNode) {
        m_audioSampleGrabberNode->Release();
        m_audioSampleGrabberNode = NULL;
    }
}

void MFPlayerSession::setAudioOutput(QPlatformAudioOutput *device)
{
    if (m_audioOutput == device)
        return;

    if (m_audioOutput)
        m_audioOutput->q->disconnect(this);

    m_audioOutput = device;
    if (m_audioOutput) {
        setMuted(m_audioOutput->q->isMuted());
        setVolume(m_audioOutput->q->volume());
        updateOutputRouting();
        connect(m_audioOutput->q, &QAudioOutput::deviceChanged, this, &MFPlayerSession::updateOutputRouting);
        connect(m_audioOutput->q, &QAudioOutput::volumeChanged, this, &MFPlayerSession::setVolume);
        connect(m_audioOutput->q, &QAudioOutput::mutedChanged, this, &MFPlayerSession::setMuted);
    }
}

void MFPlayerSession::updateOutputRouting()
{
    int currentAudioTrack = m_trackInfo[QPlatformMediaPlayer::AudioStream].currentIndex;
    if (currentAudioTrack > -1)
        setActiveTrack(QPlatformMediaPlayer::AudioStream, currentAudioTrack);
}

void MFPlayerSession::setVideoSink(QVideoSink *sink)
{
    m_videoRendererControl->setSink(sink);
}

void MFPlayerSession::setActiveTrack(QPlatformMediaPlayer::TrackType type, int index)
{
    if (!m_session)
        return;

    // Only audio track selection is currently supported.
    if (type != QPlatformMediaPlayer::AudioStream)
        return;

    const auto &nativeIndexes = m_trackInfo[type].nativeIndexes;

    if (index < -1 || index >= nativeIndexes.count())
        return;

    // Updating the topology fails if there is a HEVC video stream,
    // which causes other issues. Ignoring the change, for now.
    if (m_trackInfo[QPlatformMediaPlayer::VideoStream].format == MFVideoFormat_HEVC)
        return;

    IMFTopology *topology = nullptr;

    if (SUCCEEDED(m_session->GetFullTopology(QMM_MFSESSION_GETFULLTOPOLOGY_CURRENT, 0, &topology))) {

        m_restorePosition = position() * 10000;

        if (m_state.command == CmdStart)
            stop();

        if (m_trackInfo[type].outputNodeId != TOPOID(-1)) {
            IMFTopologyNode *node = nullptr;
            if (SUCCEEDED(topology->GetNodeByID(m_trackInfo[type].outputNodeId, &node))) {
                topology->RemoveNode(node);
                node->Release();
                m_trackInfo[type].outputNodeId = TOPOID(-1);
            }
        }
        if (m_trackInfo[type].sourceNodeId != TOPOID(-1)) {
            IMFTopologyNode *node = nullptr;
            if (SUCCEEDED(topology->GetNodeByID(m_trackInfo[type].sourceNodeId, &node))) {
                topology->RemoveNode(node);
                node->Release();
                m_trackInfo[type].sourceNodeId = TOPOID(-1);
            }
        }

        IMFMediaSource *mediaSource = m_sourceResolver->mediaSource();

        IMFPresentationDescriptor *sourcePD = nullptr;
        if (SUCCEEDED(mediaSource->CreatePresentationDescriptor(&sourcePD))) {

            if (m_trackInfo[type].currentIndex >= 0 && m_trackInfo[type].currentIndex < nativeIndexes.count())
                sourcePD->DeselectStream(nativeIndexes.at(m_trackInfo[type].currentIndex));

            m_trackInfo[type].currentIndex = index;

            if (index == -1) {
                m_session->SetTopology(MFSESSION_SETTOPOLOGY_IMMEDIATE, topology);
            } else {
                int nativeIndex = nativeIndexes.at(index);
                sourcePD->SelectStream(nativeIndex);

                IMFStreamDescriptor *streamDesc = nullptr;
                BOOL selected = FALSE;

                if (SUCCEEDED(sourcePD->GetStreamDescriptorByIndex(nativeIndex, &selected, &streamDesc))) {
                    IMFTopologyNode *sourceNode = addSourceNode(topology, mediaSource, sourcePD, streamDesc);
                    if (sourceNode) {
                        IMFTopologyNode *outputNode = addOutputNode(MFPlayerSession::Audio, topology, 0);
                        if (outputNode) {
                            if (SUCCEEDED(sourceNode->ConnectOutput(0, outputNode, 0))) {
                                sourceNode->GetTopoNodeID(&m_trackInfo[type].sourceNodeId);
                                outputNode->GetTopoNodeID(&m_trackInfo[type].outputNodeId);
                                m_session->SetTopology(MFSESSION_SETTOPOLOGY_IMMEDIATE, topology);
                            }
                            outputNode->Release();
                        }
                        sourceNode->Release();
                    }
                    streamDesc->Release();
                }
            }
            m_updatingTopology = true;
            sourcePD->Release();
        }
        topology->Release();
    }
}

int MFPlayerSession::activeTrack(QPlatformMediaPlayer::TrackType type)
{
    if (type < 0 || type >= QPlatformMediaPlayer::NTrackTypes)
        return -1;
    return m_trackInfo[type].currentIndex;
}

int MFPlayerSession::trackCount(QPlatformMediaPlayer::TrackType type)
{
    if (type < 0 || type >= QPlatformMediaPlayer::NTrackTypes)
        return -1;
    return m_trackInfo[type].metaData.count();
}

QMediaMetaData MFPlayerSession::trackMetaData(QPlatformMediaPlayer::TrackType type, int trackNumber)
{
    if (type < 0 || type >= QPlatformMediaPlayer::NTrackTypes)
        return {};

    if (trackNumber < 0 || trackNumber >= m_trackInfo[type].metaData.count())
        return {};

    return m_trackInfo[type].metaData.at(trackNumber);
}


// Copyright (C) 2016 The Qt Company Ltd and/or its subsidiary(-ies).
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "avfvideosink_p.h"

#include <private/qrhi_p.h>
#include <private/qrhimetal_p.h>
#include <private/qrhigles2_p.h>
#include <QtGui/qopenglcontext.h>

#include <AVFoundation/AVFoundation.h>
#import <QuartzCore/CATransaction.h>

#if QT_HAS_INCLUDE(<AppKit/AppKit.h>)
#include <AppKit/AppKit.h>
#endif

#if QT_HAS_INCLUDE(<UIKit/UIKit.h>)
#include <UIKit/UIKit.h>
#endif

QT_USE_NAMESPACE

AVFVideoSink::AVFVideoSink(QVideoSink *parent)
    : QPlatformVideoSink(parent)
{
}

AVFVideoSink::~AVFVideoSink()
{
}

void AVFVideoSink::setRhi(QRhi *rhi)
{
    if (m_rhi == rhi)
        return;
    m_rhi = rhi;
    if (m_interface)
        m_interface->setRhi(rhi);
}

void AVFVideoSink::setNativeSize(QSize size)
{
    if (size == nativeSize())
        return;
    QPlatformVideoSink::setNativeSize(size);
    if (m_interface)
        m_interface->nativeSizeChanged();
}

void AVFVideoSink::setVideoSinkInterface(AVFVideoSinkInterface *interface)
{
    m_interface = interface;
    if (m_interface)
        m_interface->setRhi(m_rhi);
}

// The OpengGL texture cache can apparently only handle single plane formats, so lets simply restrict to BGRA
static NSDictionary* const AVF_OUTPUT_SETTINGS_OPENGL = @{
        (NSString *)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
#ifndef Q_OS_IOS // On iOS this key generates a warning about unsupported key.
        , (NSString *)kCVPixelBufferOpenGLCompatibilityKey: @true
#endif // Q_OS_IOS
};

AVFVideoSinkInterface::~AVFVideoSinkInterface()
{
    if (m_layer)
        [m_layer release];
    freeTextureCaches();
}

void AVFVideoSinkInterface::freeTextureCaches()
{
    if (cvMetalTextureCache)
        CFRelease(cvMetalTextureCache);
    cvMetalTextureCache = nullptr;
#if defined(Q_OS_MACOS)
    if (cvOpenGLTextureCache)
        CFRelease(cvOpenGLTextureCache);
    cvOpenGLTextureCache = nullptr;
#elif defined(Q_OS_IOS)
    if (cvOpenGLESTextureCache)
        CFRelease(cvOpenGLESTextureCache);
    cvOpenGLESTextureCache = nullptr;
#endif
}

void AVFVideoSinkInterface::setVideoSink(AVFVideoSink *sink)
{
    if (sink == m_sink)
        return;

    m_sink = sink;
    if (m_sink) {
        m_sink->setVideoSinkInterface(this);
        reconfigure();
    }
}

void AVFVideoSinkInterface::setRhi(QRhi *rhi)
{
    if (m_rhi == rhi)
        return;
    freeTextureCaches();
    m_rhi = rhi;

    if (!rhi)
        return;
    if (rhi->backend() == QRhi::Metal) {
        const auto *metal = static_cast<const QRhiMetalNativeHandles *>(rhi->nativeHandles());

        // Create a Metal Core Video texture cache from the pixel buffer.
        Q_ASSERT(!cvMetalTextureCache);
        if (CVMetalTextureCacheCreate(
                        kCFAllocatorDefault,
                        nil,
                        (id<MTLDevice>)metal->dev,
                        nil,
                        &cvMetalTextureCache) != kCVReturnSuccess) {
            qWarning() << "Metal texture cache creation failed";
            m_rhi = nullptr;
        }
    } else if (rhi->backend() == QRhi::OpenGLES2) {
#if QT_CONFIG(opengl)
        setOutputSettings(AVF_OUTPUT_SETTINGS_OPENGL);

#ifdef Q_OS_MACOS
        const auto *gl = static_cast<const QRhiGles2NativeHandles *>(rhi->nativeHandles());

        auto nsGLContext = gl->context->nativeInterface<QNativeInterface::QCocoaGLContext>()->nativeContext();
        auto nsGLPixelFormat = nsGLContext.pixelFormat.CGLPixelFormatObj;

        // Create an OpenGL CoreVideo texture cache from the pixel buffer.
        if (CVOpenGLTextureCacheCreate(
                        kCFAllocatorDefault,
                        nullptr,
                        reinterpret_cast<CGLContextObj>(nsGLContext.CGLContextObj),
                        nsGLPixelFormat,
                        nil,
                        &cvOpenGLTextureCache)) {
            qWarning() << "OpenGL texture cache creation failed";
            m_rhi = nullptr;
        }
#endif
#ifdef Q_OS_IOS
        // Create an OpenGL CoreVideo texture cache from the pixel buffer.
        if (CVOpenGLESTextureCacheCreate(
                        kCFAllocatorDefault,
                        nullptr,
                        [EAGLContext currentContext],
                        nullptr,
                        &cvOpenGLESTextureCache)) {
            qWarning() << "OpenGL texture cache creation failed";
            m_rhi = nullptr;
        }
#endif
#else
        m_rhi = nullptr;
#endif // QT_CONFIG(opengl)
    }
}

void AVFVideoSinkInterface::setLayer(CALayer *layer)
{
    if (layer == m_layer)
        return;

    if (m_layer)
        [m_layer release];

    m_layer = layer;
    if (m_layer)
        [m_layer retain];

    reconfigure();
}

void AVFVideoSinkInterface::setOutputSettings(NSDictionary *settings)
{
    m_outputSettings = settings;
}

void AVFVideoSinkInterface::updateLayerBounds()
{
    if (!m_layer)
        return;
    [CATransaction begin];
    [CATransaction setDisableActions: YES]; // disable animation/flicks
    m_layer.frame = QRectF(0, 0, nativeSize().width(), nativeSize().height()).toCGRect();
    m_layer.bounds = m_layer.frame;
    [CATransaction commit];
}

#include "moc_avfvideosink_p.cpp"

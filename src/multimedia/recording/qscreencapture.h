// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QSCREENCAPTURE_H
#define QSCREENCAPTURE_H

#include <QtCore/qobject.h>
#include <QtCore/qnamespace.h>
#include <QtGui/qscreen.h>
#include <QtGui/qwindow.h>
#include <QtGui/qwindowdefs.h>
#include <QtMultimedia/qtmultimediaglobal.h>

QT_BEGIN_NAMESPACE


class QMediaCaptureSession;
class QPlatformScreenCapture;
struct QScreenCapturePrivate;

class Q_MULTIMEDIA_EXPORT QScreenCapture : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(WId windowId READ windowId WRITE setWindowId NOTIFY windowIdChanged)
    Q_PROPERTY(QWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(QScreen *screen READ screen WRITE setScreen NOTIFY screenChanged)
    Q_PROPERTY(Error error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)

public:
    enum Error
    {
        NoError,
        InternalError,
        CapturingNotSupported,
        WindowCapturingNotSupported,
        CaptureFailed,
        NotFound
    };
    Q_ENUM(Error)

    explicit QScreenCapture(QObject *parent = nullptr);
    ~QScreenCapture();

    QMediaCaptureSession *captureSession() const;

    void setWindow(QWindow *window);
    QWindow *window() const;

    void setWindowId(WId id);
    WId windowId() const;

    void setScreen(QScreen *screen);
    QScreen *screen() const;

    bool isActive() const;

    Error error() const;
    QString errorString() const;

public Q_SLOTS:
    void setActive(bool active);
    void start() { setActive(true); }
    void stop() { setActive(false); }

Q_SIGNALS:
    void activeChanged(bool);
    void errorChanged();
    void windowIdChanged(WId);
    void windowChanged(QWindow *);
    void screenChanged(QScreen *);
    void errorOccurred(QScreenCapture::Error error, const QString &errorString);

private:
    void setCaptureSession(QMediaCaptureSession *captureSession);
    QPlatformScreenCapture *platformScreenCapture() const;
    QScreenCapturePrivate *d = nullptr;
    friend class QMediaCaptureSession;
    Q_DISABLE_COPY(QScreenCapture)
};

QT_END_NAMESPACE

#endif // QSCREENCAPTURE_H

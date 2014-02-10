TARGET = tst_qdeclarativemultimediaglobal
CONFIG += warn_on qmltestcase

QT += multimedia-private

include (../qmultimedia_common/mock.pri)
include (../qmultimedia_common/mockcamera.pri)

SOURCES += \
        tst_qdeclarativemultimediaglobal.cpp

DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0

OTHER_FILES += \
    tst_qdeclarativemultimediaglobal.qml

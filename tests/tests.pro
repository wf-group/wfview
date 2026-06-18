QT += core gui qml quick testlib network xml multimedia

TEMPLATE = app
TARGET = wfview-tests
CONFIG += c++17 console
CONFIG -= app_bundle

DEFINES += OUTSIDE_SPEEX
DEFINES += RANDOM_PREFIX=wf
DEFINES += EIGEN_MPL2_ONLY
DEFINES += EIGEN_DONT_VECTORIZE
equals(QT_ARCH, i386): win32:DEFINES += EIGEN_VECTORIZE_SSE3
equals(QT_ARCH, x86_64): DEFINES += EIGEN_VECTORIZE_SSE3

INCLUDEPATH += $$PWD/../include
INCLUDEPATH += $$PWD/../src
INCLUDEPATH += $$PWD/../src/audio
!linux:INCLUDEPATH += $$PWD/../../opus/include
!linux:INCLUDEPATH += $$PWD/../../eigen
!linux:INCLUDEPATH += $$PWD/../../r8brain-free-src
!linux:INCLUDEPATH += $$PWD/../../rtaudio
win32:INCLUDEPATH += $$PWD/../../portaudio/include
win32:INCLUDEPATH += $$PWD/../../hidapi/hidapi
INCLUDEPATH += $$PWD/../src/audio/resampler
INCLUDEPATH += $$PWD/../src/audio/plugins
INCLUDEPATH += $$PWD/../src/audio/pocketfft
INCLUDEPATH += $$PWD/../src/audio/speexdspmini/src
INCLUDEPATH += $$PWD/../src/audio/speexdspmini/include
INCLUDEPATH += $$PWD/../src/audio/anr

SOURCES += \
    $$PWD/tst_main.cpp \
    $$PWD/tst_audiorouting.cpp \
    $$PWD/tst_cluster.cpp \
    $$PWD/tst_controllercontroller.cpp \
    $$PWD/tst_icomscopeframeassembler.cpp \
    $$PWD/tst_iaxclientsession.cpp \
    $$PWD/tst_modsource.cpp \
    $$PWD/tst_periodicmode.cpp \
    $$PWD/tst_radiotransportframe.cpp \
    $$PWD/tst_rigctlcompat.cpp \
    $$PWD/tst_rigctld.cpp \
    $$PWD/tst_rigprotocols.cpp \
    $$PWD/../src/cluster.cpp \
    $$PWD/../src/ControllerController.cpp \
    $$PWD/../src/audiorouting.cpp \
    $$PWD/../src/iaxclientsession.cpp \
    $$PWD/../src/radio/icomscopeframeassembler.cpp \
    $$PWD/../src/logcategories.cpp \
    $$PWD/../src/radiotransportframe.cpp \
    $$PWD/../src/rigctlcompat.cpp \
    $$PWD/../src/cachingqueue.cpp \
    $$PWD/../src/rigctld.cpp

HEADERS += \
    $$PWD/../include/cluster.h \
    $$PWD/../include/ControllerController.h \
    $$PWD/../include/audiorouting.h \
    $$PWD/../include/iaxclientsession.h \
    $$PWD/../include/icomscopeframeassembler.h \
    $$PWD/../include/logcategories.h \
    $$PWD/../include/radiotransport.h \
    $$PWD/../include/radiotransportframe.h \
    $$PWD/../include/rigctlcompat.h \
    $$PWD/../include/rigctld.h \
    $$PWD/../include/cachingqueue.h \
    $$PWD/../include/usbcontroller.h

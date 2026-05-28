QT += core gui qml quick testlib network xml

TEMPLATE = app
TARGET = wfview-tests
CONFIG += c++17 console
CONFIG -= app_bundle

DEFINES += OUTSIDE_SPEEX

INCLUDEPATH += $$PWD/../include
INCLUDEPATH += $$PWD/../src
INCLUDEPATH += $$PWD/../src/audio

SOURCES += \
    $$PWD/tst_main.cpp \
    $$PWD/tst_audiorouting.cpp \
    $$PWD/tst_cluster.cpp \
    $$PWD/tst_controllercontroller.cpp \
    $$PWD/tst_icomscopeframeassembler.cpp \
    $$PWD/tst_iaxclientsession.cpp \
    $$PWD/tst_periodicmode.cpp \
    $$PWD/tst_radiotransportframe.cpp \
    $$PWD/tst_rigctlcompat.cpp \
    $$PWD/tst_rigprotocols.cpp \
    $$PWD/../src/cluster.cpp \
    $$PWD/../src/ControllerController.cpp \
    $$PWD/../src/audiorouting.cpp \
    $$PWD/../src/iaxclientsession.cpp \
    $$PWD/../src/radio/icomscopeframeassembler.cpp \
    $$PWD/../src/logcategories.cpp \
    $$PWD/../src/radiotransportframe.cpp \
    $$PWD/../src/rigctlcompat.cpp

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
    $$PWD/../include/usbcontroller.h

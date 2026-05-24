QT += core gui qml quick testlib

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
    $$PWD/tst_controllercontroller.cpp \
    $$PWD/tst_rigprotocols.cpp \
    $$PWD/../src/ControllerController.cpp \
    $$PWD/../src/logcategories.cpp

HEADERS += \
    $$PWD/../include/ControllerController.h \
    $$PWD/../include/logcategories.h \
    $$PWD/../include/usbcontroller.h

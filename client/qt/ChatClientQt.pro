QT += core gui widgets network

CONFIG += c++17

TARGET   = ChatClientQt
TEMPLATE = app

INCLUDEPATH += ../../common

SOURCES += \
    src/main.cpp \
    src/NetworkManager.cpp \
    src/ServerListWindow.cpp \
    src/ChatWindow.cpp

HEADERS += \
    src/NetworkManager.h \
    src/ServerListWindow.h \
    src/ChatWindow.h \
    ../../common/common_shared.h \
    ../../common/common_qt.h

QMAKE_CXXFLAGS += -Wall

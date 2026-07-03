QT += core gui widgets network

CONFIG += c++17

TARGET   = ChatClientQt
TEMPLATE = app

SOURCES += \
    main.cpp \
    NetworkManager.cpp \
    ServerListWindow.cpp \
    ChatWindow.cpp

HEADERS += \
    common.h \
    NetworkManager.h \
    ServerListWindow.h \
    ChatWindow.h \
    common/common_qt.h \
    common/common_shared.h

QMAKE_CXXFLAGS += -Wall

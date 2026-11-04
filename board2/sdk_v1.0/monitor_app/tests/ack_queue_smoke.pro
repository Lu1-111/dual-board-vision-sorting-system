QT += core gui
CONFIG += c++11 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = ack_queue_smoke
SOURCES += ack_queue_smoke.cpp ../candatasource.cpp
HEADERS += ../candatasource.h ../datasource.h
QMAKE_CXXFLAGS += -Wall -Wextra -Werror

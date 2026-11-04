QT += core gui network
CONFIG += c++11 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = evidence_bridge
SOURCES += main.cpp
HEADERS += ../evidence_store.h
QMAKE_CXXFLAGS += -Wall -Wextra

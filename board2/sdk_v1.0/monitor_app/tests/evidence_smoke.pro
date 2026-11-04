QT += core gui widgets
CONFIG += c++11 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = evidence_smoke
SOURCES += evidence_smoke.cpp ../aipage.cpp
HEADERS += ../aipage.h ../datasource.h ../evidence_store.h
QMAKE_CXXFLAGS += -Wall -Wextra

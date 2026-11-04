QT += core gui widgets network
CONFIG += c++11 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = livevision_page_only_smoke
SOURCES += livevision_page_only_smoke.cpp ../livevisionpage.cpp
HEADERS += ../livevisionpage.h
QMAKE_CXXFLAGS += -Wall -Wextra

SOURCES += ../camerapreviewclient.cpp
HEADERS += ../camerapreviewclient.h

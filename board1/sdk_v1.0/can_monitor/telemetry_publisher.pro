QT += core
QT -= gui
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = telemetry_publisher
SOURCES += telemetry_publisher.cpp
QMAKE_CXXFLAGS += -Wall -Wextra

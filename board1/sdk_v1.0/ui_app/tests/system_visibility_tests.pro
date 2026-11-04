QT += core gui widgets
CONFIG += c++11 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = system_visibility_tests
SOURCES += system_visibility_tests.cpp ../systempage.cpp
HEADERS += ../systempage.h
QMAKE_CXXFLAGS += -Wall -Wextra

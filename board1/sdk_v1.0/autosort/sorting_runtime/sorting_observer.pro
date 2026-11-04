QT = core network
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = sorting-observer
SOURCES += main.cpp sorting_observer.cpp
HEADERS += sorting_observer.hpp
QMAKE_CXXFLAGS += -Wall -Wextra

QT = core
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = test-sorting-observer
SOURCES += test_observer.cpp sorting_observer.cpp
HEADERS += sorting_observer.hpp
QMAKE_CXXFLAGS += -Wall -Wextra

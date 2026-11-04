QT = core
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = test-continuous-traverse
SOURCES += test_continuous.cpp drive.cpp vision.cpp conveyor/conveyor.cpp ../sorting_runtime/sorting_observer.cpp
HEADERS += drive.hpp vision.hpp conveyor/conveyor.hpp
QMAKE_CXXFLAGS += -Wall -Wextra

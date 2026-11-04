QT = core network
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = autosort
SOURCES += main.cpp traverse/vision.cpp traverse/drive.cpp traverse/conveyor/conveyor.cpp traverse/conveyor/uio_backend.cpp sorting_runtime/sorting_observer.cpp
HEADERS += gates.hpp
QMAKE_CXXFLAGS += -Wall -Wextra

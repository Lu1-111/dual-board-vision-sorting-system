QT = core network
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = position-fix-traverse-to-reference
SOURCES += main.cpp vision.cpp drive.cpp conveyor/conveyor.cpp conveyor/uio_backend.cpp ../sorting_runtime/sorting_observer.cpp
HEADERS += vision.hpp drive.hpp conveyor/conveyor.hpp
QMAKE_CXXFLAGS += -Wall -Wextra

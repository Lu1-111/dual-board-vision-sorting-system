QT += core gui widgets
CONFIG += c++11 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = arm_park_tests
SOURCES += park_tests.cpp ../armpage.cpp ../arm_actions.cpp \
    ../conveyorpage.cpp ../sort_recipe.cpp ../sort_settings.cpp \
    ../../conveyor_linux/conveyor.cpp ../../conveyor_linux/uio_backend.cpp \
    ../../arm_servo_linux/arm_control.cpp ../../arm_servo_linux/gpio_backend.cpp \
    ../../arm_servo_linux/kinematics.cpp ../../arm_servo_linux/robot_arm.cpp
HEADERS += ../armpage.hpp ../arm_actions.hpp ../conveyorpage.hpp ../sort_recipe.hpp ../sort_settings.hpp
QMAKE_CXXFLAGS += -Wall -Wextra

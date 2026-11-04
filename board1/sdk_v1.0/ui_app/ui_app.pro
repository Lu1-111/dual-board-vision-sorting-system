QT += core gui widgets

CONFIG += c++11
CONFIG -= app_bundle

TEMPLATE = app
TARGET = hdmi_ui

SOURCES += \
    main.cpp \
    arm_console.cpp \
    dashboardwindow.cpp \
    taskpage.cpp \
    systempage.cpp \
    armpage.cpp \
    arm_actions.cpp \
    conveyorpage.cpp \
    sort_recipe.cpp \
    sort_settings.cpp \
    ../conveyor_linux/conveyor.cpp \
    ../conveyor_linux/uio_backend.cpp \
    ../arm_servo_linux/arm_control.cpp \
    ../arm_servo_linux/gpio_backend.cpp \
    ../arm_servo_linux/kinematics.cpp \
    ../arm_servo_linux/robot_arm.cpp

HEADERS += \
    dashboardwindow.h \
    arm_console.hpp \
    taskpage.h \
    systempage.h \
    armpage.hpp \
    arm_actions.hpp \
    conveyorpage.hpp \
    sort_recipe.hpp \
    sort_settings.hpp \
    ../conveyor_linux/conveyor.hpp \
    ../arm_servo_linux/arm_control.hpp \
    ../arm_servo_linux/gpio_backend.hpp \
    ../arm_servo_linux/kinematics.hpp \
    ../arm_servo_linux/robot_arm.hpp

RESOURCES += \
    resources.qrc

CXX ?= g++
CXXFLAGS ?= -std=c++11 -O2 -Wall -Wextra -Werror
CPPFLAGS += -I.
CORE = arm_control.cpp gpio_backend.cpp kinematics.cpp robot_arm.cpp
BUILD ?= build
.PHONY: all test clean
all: $(BUILD)/armctl
$(BUILD):
	mkdir -p $(BUILD)
$(BUILD)/armctl: armctl.cpp $(CORE) arm_control.hpp gpio_backend.hpp command_lease.hpp kinematics.hpp robot_arm.hpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) armctl.cpp $(CORE) -o $@
$(BUILD)/test_joints: tests/test_joints.cpp arm_control.cpp arm_control.hpp command_lease.hpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/test_joints.cpp arm_control.cpp -o $@
$(BUILD)/test_kinematics: tests/test_kinematics.cpp $(CORE) command_lease.hpp kinematics.hpp robot_arm.hpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DARM_TEST_CONTROLLER tests/test_kinematics.cpp $(CORE) -o $@
$(BUILD)/test_gpio_session: tests/test_gpio_session.cpp arm_control.cpp gpio_backend.cpp arm_control.hpp gpio_backend.hpp command_lease.hpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/test_gpio_session.cpp arm_control.cpp gpio_backend.cpp -o $@
test: $(BUILD)/test_joints $(BUILD)/test_kinematics $(BUILD)/test_gpio_session $(BUILD)/armctl
	$(BUILD)/test_joints
	$(BUILD)/test_kinematics
	$(BUILD)/test_gpio_session
	python3 tests/test_cli.py $(BUILD)/armctl arm_config.ini
	python3 tests/test_vision_xy.py
clean:
	rm -f $(BUILD)/armctl $(BUILD)/test_joints $(BUILD)/test_kinematics

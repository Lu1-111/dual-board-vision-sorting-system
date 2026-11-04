CXX ?= g++
CXXFLAGS ?= -std=c++11 -O2 -Wall -Wextra -Werror
CPPFLAGS += -I.
BUILD ?= build
EXE ?=
PYTHON ?= python3
CORE = conveyor.cpp uio_backend.cpp
.PHONY: all test clean
all: $(BUILD)/conveyorctl$(EXE)
$(BUILD):
	mkdir -p $(BUILD)
$(BUILD)/conveyorctl$(EXE): conveyorctl.cpp $(CORE) conveyor.hpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) conveyorctl.cpp $(CORE) -pthread -o $@
$(BUILD)/test_conveyor$(EXE): tests/test_conveyor.cpp $(CORE) conveyor.hpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/test_conveyor.cpp $(CORE) -pthread -o $@
test: all $(BUILD)/test_conveyor$(EXE)
	$(BUILD)/test_conveyor$(EXE)
	$(BUILD)/conveyorctl$(EXE) --simulate --config conveyor.example.ini --demo
	$(BUILD)/conveyorctl$(EXE) --simulate --config conveyor.cm.simulation.ini --demo
	$(PYTHON) tests/test_cli.py $(BUILD)/conveyorctl$(EXE)
	$(PYTHON) tests/test_calibration.py
clean:
	rm -f $(BUILD)/conveyorctl$(EXE) $(BUILD)/test_conveyor$(EXE)

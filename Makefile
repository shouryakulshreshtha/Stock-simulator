# StockSim - build with:  make        (program: ./stocksim)
#                         make test   (unit tests)
#                         make run    (build and start)
# On Windows use MinGW-w64 with the POSIX thread model (g++ -std=c++17 -pthread src/*.cpp -o stocksim.exe).

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pthread
SRC      := $(filter-out src/main.cpp,$(wildcard src/*.cpp))
OBJ      := $(SRC:src/%.cpp=build/%.o)

all: stocksim

build/%.o: src/%.cpp $(wildcard src/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

stocksim: $(OBJ) build/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/main.o: src/main.cpp $(wildcard src/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

tests/run_tests: $(OBJ) tests/test_main.cpp
	$(CXX) $(CXXFLAGS) -Isrc $^ -o $@

test: tests/run_tests
	./tests/run_tests

run: stocksim
	./stocksim

clean:
	rm -rf build stocksim tests/run_tests events.csv web/chart_data.js

.PHONY: all test run clean

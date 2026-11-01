CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Icompiler

LIB_SRC := $(wildcard compiler/*.cpp compiler/*/*.cpp)
LIB_OBJ := $(LIB_SRC:.cpp=.o)

all: ipx

ipx: $(LIB_OBJ) cli/main.o
	$(CXX) $(CXXFLAGS) -pthread -o $@ $^

tests/run_tests: $(LIB_OBJ) tests/test_main.o
	$(CXX) $(CXXFLAGS) -o $@ $^

test: tests/run_tests
	./tests/run_tests

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(LIB_OBJ) cli/main.o tests/test_main.o ipx tests/run_tests

# Fully static Linux x86-64 binary for the Netlify function (no glibc version dependency)
static: $(LIB_SRC) cli/main.cpp
	mkdir -p netlify/functions/bin
	$(CXX) -std=c++17 -O2 -Icompiler -static -pthread -o netlify/functions/bin/ipx-linux-x64 $^
	strip netlify/functions/bin/ipx-linux-x64

.PHONY: all test clean static

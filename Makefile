MKDIR   := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BASEDIR  := $(MKDIR)

CXX      := /usr/bin/gcc
CXXFLAGS := -std=c++11 -Wall -g -fopenmp -march=native -O0
LDLIBS   := -lpthread -lrt -lstdc++ -lnuma

BDIR ?= build
SRC := memory_performance.cc
OBJ := $(patsubst %.cc,$(BDIR)/%.o,$(SRC))
BIN := $(patsubst %.o,%,$(OBJ))

.PHONY: all clean

all: $(OBJ) $(BIN)
	@echo "build succeed."

$(BDIR)/%.o: %.cc
	@mkdir -p $(@D)
	$(CXX) -o $@ $(CXXFLAGS) -c $<

$(BIN): %: %.o $(OBJ)
	$(CXX) -o $@ $(CXXFLAGS) $(LDFLAGS) $< $(LDLIBS)

clean:
	rm -rf $(BDIR) 

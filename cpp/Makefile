# LOA Hybrid — build with:  mingw32-make        (Windows, MinGW)
#                           make                (Linux / macOS)
# Exact GMP counting:       mingw32-make USE_GMP=1

CXX      ?= g++
CXXFLAGS := -std=c++14 -O2 -Wall -Wextra -Iinclude
SRC      := src/common/json.cpp src/common/model.cpp src/paper2/sca_vda.cpp src/paper1/loa.cpp src/hybrid/full.cpp src/hybrid/bench.cpp src/app/summary.cpp src/hybrid/hybrid.cpp src/app/server.cpp src/app/main.cpp
LIBS     :=

ifeq ($(OS),Windows_NT)
  EXE  := bin/loa_hybrid.exe
  LIBS += -lws2_32 -static-libgcc -static-libstdc++
else
  EXE  := bin/loa_hybrid
endif

ifeq ($(USE_GMP),1)
  CXXFLAGS += -DUSE_GMP
  LIBS     += -lgmpxx -lgmp
endif

EXP_SRC  := src/common/json.cpp src/common/model.cpp src/paper2/sca_vda.cpp src/paper1/loa.cpp src/app/experiments.cpp
ifeq ($(OS),Windows_NT)
  EXP := bin/loa_experiments.exe
else
  EXP := bin/loa_experiments
endif

all: $(EXE) $(EXP)

$(EXP): $(EXP_SRC) $(wildcard include/*/*.hpp)
	@mkdir -p bin 2>/dev/null || mkdir bin
	$(CXX) $(CXXFLAGS) $(EXP_SRC) -o $@ $(filter-out -lws2_32,$(LIBS))

$(EXE): $(SRC) $(wildcard include/*/*.hpp)
	@mkdir -p bin 2>/dev/null || mkdir bin
	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(LIBS)

clean:
	rm -f $(EXE) $(EXP)

.PHONY: all clean

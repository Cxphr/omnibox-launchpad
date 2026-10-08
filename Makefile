CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
TARGET   := omnibox
SRC      := src/main.cpp

ifeq ($(OS),Windows_NT)
	TARGET := omnibox.exe
	RM     := del /Q
else
	RM     := rm -f
endif

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)

.PHONY: all run clean

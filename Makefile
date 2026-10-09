# omnibox-launchpad Makefile
# Build: make          -> release
#        make debug    -> debug with console
#        make clean    -> remove binaries
#
# Requirements: MSYS2 MinGW-w64 (g++ -std=c++17)
#   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-make

CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -flto
LDFLAGS  = -static -static-libgcc -static-libstdc++ -mwindows

SRC  = src/main.cpp
OUT  = omnibox.exe

# MinGW link flags — no -luuid (part of -lole32), no pragma-comment needed
LIBS  = -ldwmapi -lshell32 -lole32 -lgdi32 -luser32 -lcomctl32 -lcomdlg32 -lshlwapi

all: $(OUT)

$(OUT): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(OUT) $(SRC) $(LIBS) $(LDFLAGS)
	@echo [OK] $(OUT) built

debug: CXXFLAGS = -std=c++17 -g -O0 -Wall -Wextra
debug: LDFLAGS  = -static-libgcc -static-libstdc++
debug: $(OUT)

strip:
	strip $(OUT)

clean:
	@cmd /c "del /f $(OUT) 2>nul" || rm -f $(OUT)
	@echo [OK] clean

.PHONY: all debug strip clean
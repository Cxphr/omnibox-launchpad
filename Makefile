CXX = x86_64-w64-mingw32-g++
CXXFLAGS = -std=c++17 -Os -O2 -flto -ffunction-sections -fdata-sections
LDFLAGS = -static -s -mwindows -municode -Wl,--gc-sections
LIBS = -luser32 -lgdi32 -lshell32 -lshlwapi -lcomctl32 -lole32 -ldwmapi

omnibox.exe: src/main.cpp
	$(CXX) $(CXXFLAGS) src/main.cpp -o omnibox.exe $(LDFLAGS) $(LIBS)

clean:
	rm -f omnibox.exe
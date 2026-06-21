# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present ELECTRON (https://github.com/electron-linux)

CXX ?= g++
CCFLAGS = -W -Wall -std=c++11 `sdl2-config --cflags`

BINARY = sdl2notify
LIBRARIES = -lSDL2 -lSDL2_ttf -lstdc++fs
SOURCES = "sdl2notify.cpp"

all:
	$(CXX) $(CFLAGS) $(SOURCES) -o $(BINARY) $(LIBRARIES)

clean:
	rm -f $(BINARY)

.PHONY: all clean

all: rim2vtt

clean:
	rm --force --verbose -- rim2vtt

rim2vtt: rim2vtt.cpp Makefile base64.c base64.h
	g++ rim2vtt.cpp el1/gen/dbg/amalgam/el1.cpp base64.c -o rim2vtt -O0 -g -flto -l tinyxml2 -l z -Wall -Wextra -Wno-unused-parameter

# Simple makefile for utils
CC=gcc
WCC=i686-w64-mingw32-gcc
SRC=src
WBIN=win32
BIN=bin
INSTALL_DIR=~/.local/bin
LIBS += -lm

all: vz2wav cas2wav

vz2wav: $(SRC)/vz2wav.c
	$(CC) -o $(BIN)/vz2wav $(SRC)/vz2wav.c $(LIBS)
	$(WCC) -o $(WBIN)/vz2wav $(SRC)/vz2wav.c $(LIBS)

#cas2wav: $(SRC)/cas2wav.c
#	$(CC) -o $(BIN)/cas2wav $(SRC)/cas2wav.c
#	$(WCC) -o $(WBIN)/cas2wav $(SRC)/cas2wav.c

clean:
	rm -f $(WBIN)/* $(BIN)/* *~ $(SRC)/*~ 

install:
	cp $(BIN)/* $(INSTALL_DIR)/

CC = gcc
FLAGS = -O2 -fPIE -pie -fstack-protector-strong -fstack-clash-protection -fcf-protection=full -D_FORTIFY_SOURCE=3 -Wl,-z,relro,-z,now -s
DBGFLAGS = -DDEBUG
LIBS = $(shell pkg-config --cflags --libs libsystemd)
SRC = src/battery_monitor.c
TARGET = battery_monitor
INSTALL_DIR = /usr/local/bin/

.PHONY: all install uninstall clean debug $(TARGET)

all: $(TARGET)

install: $(TARGET)
	sudo mv -f $(TARGET) $(INSTALL_DIR)

uninstall:
	kill -s SIGTERM $$(pgrep $(TARGET)); true
	sudo rm -f $(INSTALL_DIR)$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET)_debug

debug:
	$(CC) $(FLAGS) $(DBGFLAGS) $(SRC) -o $(TARGET)_debug $(LIBS)

$(TARGET):
	$(CC) $(FLAGS) $(SRC) -o $(TARGET) $(LIBS)


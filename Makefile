CC = gcc
CFLAGS = -O2 -fPIE -pie -fstack-protector-strong -fstack-clash-protection -fcf-protection=full -D_FORTIFY_SOURCE=3 -Wl,-z,relro,-z,now -s $(shell pkg-config --cflags libsystemd)
CDBGFLAGS = -DDEBUG
LFLAGS = $(shell pkg-config --libs libsystemd)
SRC = src/battery_monitor.c
TARGET = battery_monitor
INSTALL_DIR = /usr/local/bin/

.PHONY: all install uninstall clean debug $(TARGET)

all: $(TARGET)

install: $(TARGET)
	sudo cp $(TARGET) $(INSTALL_DIR)$(TARGET)

uninstall:
	sh -c 'kill -s SIGTERM pgrep ${TARGET} 2>/dev/null || kill -9 pgrep ${TARGET} 2>/dev/null; true'
	sudo rm $(INSTALL_DIR)$(TARGET)

clean:
	rm $(TARGET)*

debug:
	$(CC) $(CFLAGS) $(CDBGFLAGS) $(SRC) -o $(TARGET)_debug $(LFLAGS)

$(TARGET):
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LFLAGS)


# 取得 GIO / GLib 的編譯與連結參數
CFLAGS  := $(shell pkg-config --cflags glib-2.0 gio-2.0)
LDFLAGS := $(shell pkg-config --libs glib-2.0 gio-2.0)

all: ble_server
	@echo ""
	@echo "Start compiling......"
	@echo ""

ble_server: clean
	gcc $(CFLAGS) -o ble_server main.c -llgpio -lmosquitto -lpthread $(LDFLAGS)

clean:
	rm -f ble_server
SRC=lemonbar-status.c
TARGET=lemonbar-status

all: $(TARGET)

$(TARGET): $(SRC)

clean:
	rm -rf .o lemonbar-status

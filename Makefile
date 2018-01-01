SRC=lemonbar-status.c
TARGET=lemonbar-status

all: strip

strip: $(TARGET)
	strip $(TARGET)

$(TARGET): $(SRC)

debug: $(TARGET)-debug

$(TARGET)-debug: $(SRC)
	cc -g -o $(TARGET) $(SRC)

clean:
	rm -rf $(TARGET) *.o *.s

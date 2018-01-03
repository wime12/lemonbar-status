SRC=lemonbar-status.c
TARGET=lemonbar-status
DEBUGTARGET=$(TARGET)-debug
INCLUDES=-I/usr/X11R6/include
LIBPATHS=-L/usr/X11R6/lib
LIBS=-lxcb -lxcb-randr
CHECKFLAGS=-Wall

all: strip

strip: $(TARGET)
	strip $(TARGET)

$(TARGET): $(SRC)
	cc -O2 -pipe -o $(TARGET) $(INCLUDES) $(LIBPATHS) $(LIBS) $(.ALLSRC)

debug: $(DEBUGTARGET)

$(TARGET)-debug: $(SRC)
	cc -g -o $(DEBUGTARGET) $(INCLUDES) $(LIBPATHS) $(LIBS) $(.ALLSRC)

check: $(SRC)
	cc $(CHECKFLAGS) -o /dev/null $(INCLUDES) $(LIBPATHS) $(LIBS) $(.ALLSRC)

clean:
	rm -rf $(TARGET) $(DEBUGTARGET) *.o *.s a.out *.core

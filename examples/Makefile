SRC := TftGifStreamer.c
OBJS := $(SRC:.cc=.o)
TARGET ?= TftGifStreamer

DBFLAGS = -D__LINUX__ -O -g -Wall -Werror
CFLAGS += $(DBFLAGS)

$(info INCLUDES=$(INCLUDES))
$(info OBJS=$(OBJS))
$(info CFLAGS=$(CFLAGS))

all: $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm -f *.o $(TARGET) *.elf *.map *.txt
	-rm -rf TftGifStreamer.dSYM


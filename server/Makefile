SRC := aesdsocket.c
TARGET = aesdsocket
OBJS := $(SRC:.c=.o)
CFLAGS := -g -Wall -Werror

ifdef CROSS_COMPILE
CC := $(CROSS_COMPILE)gcc
$(info CC=$(shell which $(CC)))
endif

all: clean $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) $(LDFLAGS)

clean:
	-rm -f *.o $(TARGET) *.elf *.map

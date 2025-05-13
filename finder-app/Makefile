SRC := writer.c
TARGET = writer
OBJS := $(SRC:.c=.o)

ifdef CROSS_COMPILE
CC := $(CROSS_COMPILE)gcc
$(info "Overwriting CC=$(CC)")
endif


all: $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) $(LDFLAGS)

clean:
	-rm -f *.o $(TARGET) *.elf *.map

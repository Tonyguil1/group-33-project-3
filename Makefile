CC      = gcc
CFLAGS  = -Wall -Wextra -g -Iinclude
SRCS    = src/main.c src/fat32.c src/shell.c src/directory.c src/file.c
TARGET  = bin/filesys

all: bin $(TARGET)

bin:
	mkdir -p bin

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
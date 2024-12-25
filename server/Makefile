# Makefile for the aesdsocket

# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Werror -g

# Target executable
TARGET = aesdsocket

# Source files
SRCS = aesdsocket.c client_flow.c

# Default target
all: $(TARGET)

# Build the aesdsocket application
$(TARGET): $(SRCS)
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

# Clean up build files
clean:
	rm -f $(TARGET) *.o

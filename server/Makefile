# Makefile for the aesdsocket

# Compiler
CC ?= $(CROSS_COMPILE)gcc

# Compiler flags
CFLAGS ?= -g -Wall -Werror

# Linker flags
LDFLAGS ?= -lpthread -lrt

# Target executable
TARGET ?= aesdsocket

# Source files
SRCS = aesdsocket.c client_flow.c packet_fragment.c

# Object files
OBJS = $(SRCS:.c=.o)

# Default target
all: $(TARGET)

# Build the aesdsocket application
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

# Compile source files into object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Build the aesdsocket application
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

# Clean up build files
clean:
	rm -f $(TARGET) $(OBJS)

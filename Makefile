# Define the target executable
TARGET = thingino-button

# Define the source files
SRCS = thingino-button.c cJSON.c

# Use CROSS_COMPILE if defined, otherwise default to the native gcc
CC = $(CROSS_COMPILE)gcc

# Define the compiler flags
CFLAGS = -Wall -Wextra -Os

# Define the linker flags
LDFLAGS =

# Define the object files (replace .c with .o)
OBJS = $(SRCS:.c=.o)

# The default target
all: $(TARGET)

# Rule to build the target executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Rule to compile the source files into object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up the build files
clean:
	rm -f $(OBJS) $(TARGET)

# Phony targets
.PHONY: all clean

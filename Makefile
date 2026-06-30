CC = gcc
CFLAGS = -Wall -g -I. # -I. to find local headers like cJSON.h, json_table.h
LDFLAGS = -lm # Link math library for cJSON if needed (e.g. for NAN, sqrt)

SRCS = cJSON.c json_path.c json_table.c main.c
OBJS = $(SRCS:.c=.o)
TARGET = json_table_demo

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

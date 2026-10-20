CC = gcc
CFLAGS ?= -Wall -Wextra -O2 -Isrc

ifeq ($(OS),Windows_NT)
    LIBS = -lws2_32
    TARGET = pubsub_dsl.exe
    RM = del /Q /F
else
    LIBS =
    TARGET = pubsub_dsl
    RM = rm -f
endif

SRCS = src/logger.c src/net.c src/lexer.c src/ast.c src/parser.c src/runtime.c src/node_listener.c src/web_server.c src/main.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	./$(TARGET) test

clean:
	-$(RM) src/*.o *.log $(TARGET) pubsub_dsl pubsub_dsl.exe

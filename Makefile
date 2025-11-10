CC      := clang
CFLAGS  := -std=c17 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -g
LDFLAGS := 
SRC     := src/main.c src/parser.c src/executor.c src/builtins.c src/history.c
OBJ     := $(SRC:.c=.o)
BIN     := myshell

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean

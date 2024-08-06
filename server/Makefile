
SRC := aesdsocket.c
OBJ := aesdsocket
OBJ2 :=aesdsocketdata

LDFLAGS += -Wl,--hash-style=gnu

CC ?= $(CROSS_COMPILE)gcc


all:
	$(CC) $(CFLAGS) -o $(OBJ) $(SRC) $(LDFLAGS)
default:
	$(CC) -o $(OBJ) $(SRC)	
clean:
	rm $(OBJ)	


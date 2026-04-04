CC = gcc
CFLAGS = -Wall -g -D_FILE_OFFSET_BITS=64
LDFLAGS = -lpthread

BINDIR = bin
TARGET_SERVER = $(BINDIR)/server
TARGET_CLIENT = $(BINDIR)/client

SERVER_SRC = server/server.c common/utils.c common/log.c
CLIENT_SRC = client/client.c common/utils.c

all: $(BINDIR) $(TARGET_SERVER) $(TARGET_CLIENT)

$(BINDIR):
	mkdir -p $(BINDIR)

$(TARGET_SERVER): $(SERVER_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(SERVER_SRC) $(LDFLAGS)

$(TARGET_CLIENT): $(CLIENT_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_SRC)


clean:
	rm -rf $(BINDIR)

.PHONY: all clean
# --- Variables ---
CC = gcc
CFLAGS = -g -Wall -Iinclude
THREAD_LIBS = -pthread

.PHONY: all clean

# --- Targets ---
all: bin/name_server bin/storage_server bin/client

# Target for the Name Server
bin/name_server: name_server/nm.c
	@mkdir -p bin
	$(CC) $(CFLAGS) $(THREAD_LIBS) -o bin/name_server name_server/nm.c
	@echo "Compiled Name Server!"

# Target for the Storage Server
bin/storage_server: storage_server/ss.c
	@mkdir -p bin
	$(CC) $(CFLAGS) $(THREAD_LIBS) -o bin/storage_server storage_server/ss.c
	@echo "Compiled Storage Server!"

# Target for the Client
bin/client: client/client.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o bin/client client/client.c
	@echo "Compiled Client!"

clean:
	@echo "Cleaning compiled executables..."
	@rm -f bin/name_server bin/storage_server bin/client
	@echo "Cleaning logs and metadata..."
	@rm -f nm.log ss.log nm_metadata.dat
	@echo "Cleaning all stored files..."
	@rm -rf ss_data
	@echo "Cleanup complete."
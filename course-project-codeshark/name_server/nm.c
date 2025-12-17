#include "protocol.h"
//our file which tells this code of our own defined stuff for the network protocol (shared constants, command strings, and response codes)

#include "config.h" 

#include <stdio.h>
//for printf and perror

#include <stdlib.h>
//for exit

#include <string.h>
//for bzero or memset

#include <unistd.h>
//for read write close all

#include <sys/socket.h>
//for main socket functions

#include <netinet/in.h>
//for the struct sockaddr_in and htons()/htonl() macros.

#include <pthread.h>
//for threading

#include <stdbool.h>
//for bool 

#include <arpa/inet.h>  
// For inet_pton

#include <errno.h>
// For errno

#include <time.h>
#include <fcntl.h> // For fcntl
#include <time.h>

// --- User Registry (records all users ever connected) ---
typedef struct {
    char username[MAX_USERNAME_LEN];
} UserRecord;

UserRecord g_user_registry[500];
int g_user_registry_count = 0;

// Mutex (optional but recommended)
pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;

// Globals for logging
FILE* g_log_fp = NULL;
pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    char username[MAX_USERNAME_LEN];
    char permission; // Will hold 'R' or 'W'
} AclEntry;

typedef struct{
    int conn_fd;
    char username[MAX_USERNAME_LEN];
}Client;

# define MAX_CLIENTS 50 // WHAT IS MAX NO OF CLIENTS WE ARE ALLOWING, CAN BE CHANGED LATER
Client g_client_list[MAX_CLIENTS];
int g_client_count=0;
pthread_mutex_t client_list_mutex=PTHREAD_MUTEX_INITIALIZER;

// 3. File Map (Which file is on which SS?)
typedef struct {
    char path[MAX_PATH_LEN];
    int ss_index;
    char owner[MAX_USERNAME_LEN];
    AclEntry acl_list[MAX_CLIENTS]; // Max permissions = max clients
    int acl_count;                  // Counter for how many are in the list
    int word_count;
    int char_count;
    time_t created_at;
    time_t modified_at;
    time_t accessed_at;
    bool is_directory;              // NEW: Flag for folder vs file
}FileMapEntry;

#define ASCII_SIZE 256

typedef struct TrieNode {
    struct TrieNode* children[ASCII_SIZE];
    // -1 = Not end of a file
    // >= 0 = End of a file; value is the index in g_file_map
    int file_index;
} TrieNode;

typedef struct CacheEntry {
    char filename[MAX_PATH_LEN];
    int file_index;
    struct CacheEntry* prev;
    struct CacheEntry* next;
} CacheEntry;

#define MAX_FILES 1000
#define MAX_CACHE_SIZE 50

FileMapEntry g_file_map[MAX_FILES];
int g_file_count = 0;
pthread_mutex_t file_map_mutex = PTHREAD_MUTEX_INITIALIZER;

// Access Request System
typedef struct {
    int request_id;
    char requester[MAX_USERNAME_LEN];
    char filename[MAX_PATH_LEN];
    char permission;
    char status;
    time_t requested_at;
    time_t processed_at;
} AccessRequest;

#define MAX_ACCESS_REQUESTS 500
AccessRequest g_access_requests[MAX_ACCESS_REQUESTS];
int g_access_request_count = 0;
int g_next_request_id = 1;
pthread_mutex_t access_request_mutex = PTHREAD_MUTEX_INITIALIZER;

TrieNode* g_file_trie_root;
CacheEntry* g_cache_head = NULL;
CacheEntry* g_cache_tail = NULL;
int g_cache_size = 0;

// ============================================================================
// REPLICATION & FAULT TOLERANCE DATA STRUCTURES
// ============================================================================

typedef enum {
    SS_STATUS_ONLINE,
    SS_STATUS_OFFLINE,
    SS_STATUS_RECOVERING
} SSStatus;

typedef struct {
    int conn_fd;
    char ip[INET_ADDRSTRLEN];
    int client_port;
    SSStatus status;
    time_t last_heartbeat;
    int pending_write_count;  // Number of async writes pending
} StorageServerExt;

// Extended SS list for replication
StorageServerExt g_ss_list_ext[MAX_SS];
int g_ss_count_ext = 0;
pthread_mutex_t ss_list_mutex_ext = PTHREAD_MUTEX_INITIALIZER;

// Track replicas for each file (indices into g_ss_list_ext)
typedef struct {
    char path[MAX_PATH_LEN];
    int replica_ss_indices[REPLICATION_FACTOR];  // [0] = primary, [1,2] = replicas
    int replica_count;  // How many replicas are currently available
} FileReplicationEntry;

#define MAX_FILE_REPLICAS MAX_FILES
FileReplicationEntry g_file_replicas[MAX_FILE_REPLICAS];
int g_file_replica_count = 0;
pthread_mutex_t replica_mutex = PTHREAD_MUTEX_INITIALIZER;

// Async write queue
typedef struct {
    char filename[MAX_PATH_LEN];
    char operation[32];  // "NM_CREATE", "NM_DELETE", "NM_WRITE", etc.
    int target_ss_index;
    time_t queued_at;
} AsyncWriteTask;

AsyncWriteTask g_async_write_queue[MAX_PENDING_WRITES * MAX_SS];
int g_async_write_count = 0;
pthread_mutex_t async_write_mutex = PTHREAD_MUTEX_INITIALIZER;

// ============================================================================
// END REPLICATION DATA STRUCTURES
// ============================================================================


void* handle_client_commands(void* arg);
void do_create(int client_fd, char* username, char* filename);
void do_read(int client_fd, char* username, char* filename);
void do_write(int client_fd, char* username, char* filename);
void do_exec(int client_fd, char* username, char* filename);
void do_undo(int client_fd, char* username, char* filename);
void do_add_access(int client_fd, char* requester_username, char* filename, char* target_user, char permission_flag);
void do_delete(int client_fd, char* requester_username, char* filename);
void do_list_users(int client_fd);
void do_view(int client_fd, char* requester_username, char* flags);
void do_info(int client_fd, char* requester_username, char* filename);
void do_rem_access(int client_fd, char* requester_username, char* filename, char* target_user);
void do_request_access(int client_fd, char* requester_username, char* filename, char permission_flag);
void do_view_requests(int client_fd, char* requester_username, char* filename);
void do_approve_request(int client_fd, char* requester_username, int req_id);
void do_deny_request(int client_fd, char* requester_username, int req_id);
void do_my_requests(int client_fd, char* requester_username);
void do_create_folder(int client_fd, char* username, char* foldername);
void do_move(int client_fd, char* username, char* filename, char* dest_folder);
void do_view_folder(int client_fd, char* username, char* foldername);
TrieNode* create_trie_node();
void trie_insert(const char* filename, int file_index);
int trie_search(const char* filename);
void trie_remove(const char* filename);
void trie_update_index(const char* filename, int new_index);
void cache_move_to_front(CacheEntry* entry);
void cache_evict_last();
CacheEntry* cache_find(const char* filename);
void cache_add(const char* filename, int file_index);
void cache_remove(const char* filename);
void registry_add_user(const char* name);
int registry_has_user(const char* name);

// Replication & Fault Tolerance Functions
void init_replication_system();
void* heartbeat_thread(void* arg);
void* async_write_thread(void* arg);
int select_replica_ss(int exclude_ss_index);
void enqueue_async_write(const char* filename, const char* operation, int target_ss);
void handle_ss_failure(int failed_ss_index);
void* handle_ss_recovery(void* arg);



//creates a new, empty TrieNode and returns a pointer to the newly allocated TrieNode.
TrieNode* create_trie_node() {
    TrieNode* node = (TrieNode*)malloc(sizeof(TrieNode));
    if (node) {
        node->file_index = -1; // Not the end of a path
        memset(node->children, 0, sizeof(node->children));
    }
    return node;
}

//Inserts a file's index into the global Trie. Assumes g_file_trie_root is already initialized. The filename is the full path to insert. The file_index is the index in g_file_map to store.
void trie_insert(const char* filename, int file_index) {
    TrieNode* current = g_file_trie_root;

    for (int i = 0; filename[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)filename[i];
        if (current->children[ch] == NULL) {
            current->children[ch] = create_trie_node();
        }
        current = current->children[ch];
    }
    // At the end of the string, mark it as a file
    current->file_index = file_index;
}

//It searches the global Trie for a filename. The filename is the full path to search for. It returns the file's index in g_file_map if found, otherwise -1.
int trie_search(const char* filename) {
    TrieNode* current = g_file_trie_root;

    for (int i = 0; filename[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)filename[i];
        if (current->children[ch] == NULL) {
            return -1; // Path does not exist
        }
        current = current->children[ch];
    }
    
    // At the end, return the index.
    // This will be -1 if the path is just a prefix (e.g., "foo")
    // but not a complete file (e.g., only "foo/bar.txt" exists).
    return current->file_index;
}

//It lazy removes a file from the Trie by un-marking it (with -1)
void trie_remove(const char* filename) {
    TrieNode* current = g_file_trie_root;

    for (int i = 0; filename[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)filename[i];
        if (current->children[ch] == NULL) {
            return; // File doesn't exist anyway
        }
        current = current->children[ch];
    }
    
    // "Remove" it by setting its index to -1
    current->file_index = -1;
}

//It updates the file index for an existing entry in the Trie (Used when a file is moved in the g_file_map array).
void trie_update_index(const char* filename, int new_index) {
    TrieNode* current = g_file_trie_root;

    for (int i = 0; filename[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)filename[i];
        if (current->children[ch] == NULL) {
            return; // File doesn't exist, can't update
        }
        current = current->children[ch];
    }
    
    // Update the index
    current->file_index = new_index;
}

// (It helps the NM act as a client)
int connect_to_server(const char* ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("connect_to_server: socket");
        return -1;
    }
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        perror("connect_to_server: inet_pton");
        close(sock);
        return -1;
    }
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect_to_server: connect");
        close(sock);
        return -1;
    }
    return sock;
}

void log_event(const char* message) {
    char time_str[100];
    time_t now = time(NULL);
    struct tm ltime; // A local struct for the thread-safe version

    // Use thread-safe localtime_r
    localtime_r(&now, &ltime); 
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &ltime);

    // Lock, write, unlock
    pthread_mutex_lock(&g_log_mutex);
    if (g_log_fp) {
        fprintf(g_log_fp, "[%s] %s\n", time_str, message);
        fflush(g_log_fp); // Ensure it writes immediately
    }
    pthread_mutex_unlock(&g_log_mutex);
}

/**
 * ============================================================================
 * SECTION: LRU Cache Helper Functions
 *
 * These functions also assume the file_map_mutex is HELD by the caller.
 * ============================================================================
 */

// Moves an existing cache entry to be the head (most recent).
void cache_move_to_front(CacheEntry* entry) {
    if (entry == g_cache_head) {
        return; // Already at the front
    }

    // Unlink from its current position
    if (entry->prev) {
        entry->prev->next = entry->next;
    }
    if (entry->next) {
        entry->next->prev = entry->prev;
    }

    // Check if it was the tail
    if (entry == g_cache_tail) {
        g_cache_tail = entry->prev;
    }

    // Link at the front
    entry->next = g_cache_head;
    entry->prev = NULL;
    if (g_cache_head) {
        g_cache_head->prev = entry;
    }
    g_cache_head = entry;

    // If the list was empty, it's also the tail
    if (g_cache_tail == NULL) {
        g_cache_tail = entry;
    }
}

// Evicts the last (least recently used) entry from the cache.
void cache_evict_last() {
    if (g_cache_tail == NULL) {
        return; // Cache is empty
    }

    CacheEntry* old_tail = g_cache_tail;
    
    // Update the tail pointer
    g_cache_tail = old_tail->prev;

    if (g_cache_tail) {
        g_cache_tail->next = NULL; // New tail has no next
    } else {
        g_cache_head = NULL; // Cache is now empty
    }

    free(old_tail);
    g_cache_size--;
}

// Finds a file in the cache by its name.
// Returns a pointer to the CacheEntry if found, otherwise NULL.
CacheEntry* cache_find(const char* filename) {
    CacheEntry* current = g_cache_head;
    while (current) {
        if (strcmp(current->filename, filename) == 0) {
            return current; // Found
        }
        current = current->next;
    }
    return NULL; // Not found
}

// Adds a new file index to the front of the cache.
// Evicts the last entry if the cache is full.
void cache_add(const char* filename, int file_index) {
    // 1. Create the new entry
    CacheEntry* new_entry = (CacheEntry*)malloc(sizeof(CacheEntry));
    if (!new_entry) {
        perror("malloc cache_add");
        return; // Failed to add
    }
    strcpy(new_entry->filename, filename);
    new_entry->file_index = file_index;
    new_entry->prev = NULL;

    // 2. Link at the front
    new_entry->next = g_cache_head;
    if (g_cache_head) {
        g_cache_head->prev = new_entry;
    }
    g_cache_head = new_entry;

    // 3. If list was empty, it's also the tail
    if (g_cache_tail == NULL) {
        g_cache_tail = new_entry;
    }

    // 4. Update size and evict if full
    g_cache_size++;
    if (g_cache_size > MAX_CACHE_SIZE) {
        cache_evict_last();
    }
}

// Removes an entry from the cache by its name.
// (Used for cache invalidation, e.g., on delete).
void cache_remove(const char* filename) {
    CacheEntry* entry = cache_find(filename);
    if (entry == NULL) {
        return; // Not in cache
    }

    // Unlink it
    if (entry->prev) {
        entry->prev->next = entry->next;
    }
    if (entry->next) {
        entry->next->prev = entry->prev;
    }

    // Update head/tail pointers if necessary
    if (entry == g_cache_head) {
        g_cache_head = entry->next;
    }
    if (entry == g_cache_tail) {
        g_cache_tail = entry->prev;
    }

    free(entry);
    g_cache_size--;
}

// --- PERSISTENCE FUNCTIONS ---

// Saves the entire file map and count to disk
void save_metadata_to_disk() {
    printf("[NM-Persist] Saving metadata to disk...\n");
    
    // We lock the mutex to ensure no other thread
    // is modifying the list while we save.
    // pthread_mutex_lock(&file_map_mutex);
    
    FILE* fp = fopen("nm_metadata.dat", "w");
    if (fp == NULL) {
        perror("fopen (save_metadata_to_disk)");
        // pthread_mutex_unlock(&file_map_mutex);
        return;
    }

    // 1. Write the count
    if (fwrite(&g_file_count, sizeof(int), 1, fp) != 1) {
        printf("[NM-Persist] Error writing file count.\n");
    }

    // 2. Write the entire array
    if (fwrite(g_file_map, sizeof(FileMapEntry), g_file_count, fp) != g_file_count) {
        printf("[NM-Persist] Error writing file map data.\n");
    }

    fclose(fp);
    // pthread_mutex_unlock(&file_map_mutex);
    printf("[NM-Persist] Save complete.\n");
}

// Loads the entire file map and count from disk
void load_metadata_from_disk() {
    printf("[NM-Persist] Loading metadata from disk...\n");
    
    // We lock here just to be safe, though this should
    // only be called once at startup.
    pthread_mutex_lock(&file_map_mutex);

    FILE* fp = fopen("nm_metadata.dat", "r");
    if (fp == NULL) {
        // This is not an error, it just means no file exists yet.
        printf("[NM-Persist] No metadata file found. Starting fresh.\n");
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }

    // 1. Read the count
    if (fread(&g_file_count, sizeof(int), 1, fp) != 1) {
        printf("[NM-Persist] Error reading file count.\n");
        g_file_count = 0; // Reset on error
    }

    // 2. Read the entire array
    if (fread(g_file_map, sizeof(FileMapEntry), g_file_count, fp) != g_file_count) {
        printf("[NM-Persist] Error reading file map data.\n");
        g_file_count = 0; // Reset on error
    }

    fclose(fp);
    pthread_mutex_unlock(&file_map_mutex);
    printf("[NM-Persist] Load complete. %d files loaded.\n", g_file_count);
}

void build_trie_from_map() {
    printf("[NM] Building Trie from loaded metadata...\n");
    pthread_mutex_lock(&file_map_mutex);
    for (int i = 0; i < g_file_count; i++) {
        trie_insert(g_file_map[i].path, i);
    }
    pthread_mutex_unlock(&file_map_mutex);
    printf("[NM] Trie build complete.\n");
}

void do_exec(int client_fd, char* username, char* filename) {

    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: EXEC, FILE: %s", username, filename);
    log_event(log_msg);
    printf("Client %s requesting EXEC: %s\n", username, filename);

    char resp_buf[MAX_MSG_LEN];

    // 1. Lock the central mutex
    pthread_mutex_lock(&file_map_mutex);

    int file_index = -1;

    // 2. Check cache (fastest)
    CacheEntry* entry = cache_find(filename);

    if (entry) {
        file_index = entry->file_index;
        cache_move_to_front(entry);
    } else {
        file_index = trie_search(filename);
        if (file_index != -1) {
            cache_add(filename, file_index);
        }
    }

    // 5. Handle "Not Found"
    if (file_index == -1) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_NOT_FOUND);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }

    FileMapEntry* file = &g_file_map[file_index];

    // --- SAFETY CHECK ---
    if (file->is_directory) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s Cannot perform file operation on a folder\n", RESP_BAD_REQ);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    // --------------------

    file->accessed_at = time(NULL);

    // 6. ACL Check
    bool has_access = false;
    if (strcmp(username, file->owner) == 0) has_access = true;
    if (!has_access) {
        for (int i = 0; i < file->acl_count; i++) {
            if (strcmp(username, file->acl_list[i].username) == 0) {
                has_access = true;
                break;
            }
        }
    }
    if (!has_access) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_FORBIDDEN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }

    int ss_index = file->ss_index;
    pthread_mutex_unlock(&file_map_mutex);

// 4. Get SS client-facing IP/port
    pthread_mutex_lock(&ss_list_mutex_ext);       
    char ss_ip[INET_ADDRSTRLEN];
    int ss_port = g_ss_list_ext[ss_index].client_port;
    strcpy(ss_ip, g_ss_list_ext[ss_index].ip);        
    pthread_mutex_unlock(&ss_list_mutex_ext);      

    // 8. NM connects TO SS
    int ss_sock = connect_to_server(ss_ip, ss_port);
    if (ss_sock < 0) {
        printf("EXEC: NM failed to connect to SS\n");
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SS_DOWN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        return;
    }
    printf("EXEC: NM connected to SS successfully.\n");

    // 9. Request file from SS
    char req_buf[MAX_MSG_LEN];
    char file_buf[4096];
    snprintf(req_buf, MAX_MSG_LEN, "%s %s\n", SS_GET_FILE, filename);
    send(ss_sock, req_buf, strlen(req_buf), 0);

    // 10. Read file content from SS
    memset(file_buf, 0, sizeof(file_buf));
    int bytes_read = read(ss_sock, file_buf, sizeof(file_buf) - 1);
    if (bytes_read <= 0) {
        printf("EXEC: NM failed to read file from SS\n");
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        close(ss_sock);
        return;
    }
    file_buf[bytes_read] = '\0';
    close(ss_sock);
    printf("EXEC: NM received file content from SS.\n");

    // 11. Create temp file
    char temp_filename[] = "/tmp/nm_exec_XXXXXX";
    int temp_fd = mkstemp(temp_filename); 
    if (temp_fd < 0) {
        perror("mkstemp");
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        return;
    }
    write(temp_fd, file_buf, bytes_read);
    close(temp_fd);

    // 12. Execute with popen()
    char cmd_buf[MAX_PATH_LEN + 10];
    snprintf(cmd_buf, sizeof(cmd_buf), "sh %s 2>&1", temp_filename);

    FILE* pipe = popen(cmd_buf, "r");
    if (!pipe) {
        perror("popen");
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        remove(temp_filename);
        return;
    }

    // 13. Send 200 OK
    snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_OK);
    send(client_fd, resp_buf, strlen(resp_buf), 0);

    // 14. Pipe output
    char pipe_buf[1024];
    while (fgets(pipe_buf, sizeof(pipe_buf), pipe) != NULL) {
        send(client_fd, pipe_buf, strlen(pipe_buf), 0);
    }

    pclose(pipe);
    remove(temp_filename);
    printf("EXEC: Command executed and output sent to client.\n");

    // Send the special "End of Exec" message
    snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_EXEC_DONE);
    send(client_fd, resp_buf, strlen(resp_buf), 0);
    return; // Return (keeps connection alive)
}

void do_create(int client_fd, char* username, char* filename) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: CREATE, FILE: %s", username, filename);
    log_event(log_msg);

    char resp_buf[MAX_MSG_LEN];
    int ss_index = -1;
    int ss_fd = -1;

    pthread_mutex_lock(&file_map_mutex);

    // 1. Check for conflicts
    int file_index = trie_search(filename);
    if (file_index != -1) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_CONFLICT);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    // 2. Select an ONLINE Primary SS
    pthread_mutex_lock(&ss_list_mutex_ext);
    
    if (g_ss_count_ext == 0) {
        pthread_mutex_unlock(&ss_list_mutex_ext);
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SS_DOWN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    // Start searching from a round-robin index
    int start_idx = g_file_count % g_ss_count_ext;
    int attempts = 0;
    
    // Loop to find the first ONLINE server
    while (attempts < g_ss_count_ext) {
        int curr_idx = (start_idx + attempts) % g_ss_count_ext;
        if (g_ss_list_ext[curr_idx].status == SS_STATUS_ONLINE) {
            ss_index = curr_idx;
            ss_fd = g_ss_list_ext[curr_idx].conn_fd;
            break;
        }
        attempts++;
    }
    
    // If no online server found
    if (ss_index == -1) {
        pthread_mutex_unlock(&ss_list_mutex_ext);
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SS_DOWN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }

    // We found a server! Unlock file map (but keep SS list locked for the transaction)
    pthread_mutex_unlock(&file_map_mutex);

    // 4. Send command while holding the lock
    char command_buf[MAX_MSG_LEN];
    sprintf(command_buf, "%s %s\n", NM_CREATE, filename);
    
    // FIX: Use MSG_NOSIGNAL to prevent crash if SS is dead
    if (send(ss_fd, command_buf, strlen(command_buf), MSG_NOSIGNAL) < 0) {
        perror("Failed to send to SS");
        // Since send failed, this SS is likely dead. 
        // We can mark it offline or just report error.
        pthread_mutex_unlock(&ss_list_mutex_ext);
        
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SS_DOWN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        return;
    }
    
    // 5. Read ACK
    char ss_resp[MAX_MSG_LEN];
    memset(ss_resp, 0, MAX_MSG_LEN);
    int bytes_read = read(ss_fd, ss_resp, MAX_MSG_LEN - 1);
    
    pthread_mutex_unlock(&ss_list_mutex_ext);
    
    if (bytes_read <= 0) {
        printf("[CREATE] SS failed to respond\n");
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        return;
    }
    
    ss_resp[bytes_read] = '\0';
    char *first_line = strtok(ss_resp, "\n");
    
    // --- Phase 3: Commit changes ---
    
    if (first_line && strncmp(first_line, RESP_OK, strlen(RESP_OK)) == 0) {
        pthread_mutex_lock(&file_map_mutex);

        // Re-check conflict (race condition safety)
        file_index = trie_search(filename);
        if (file_index != -1) {
            snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_CONFLICT);
            pthread_mutex_unlock(&file_map_mutex);
            send(client_fd, resp_buf, strlen(resp_buf), 0);
            return;
        }

        // Commit to metadata
        strcpy(g_file_map[g_file_count].path, filename);
        strcpy(g_file_map[g_file_count].owner, username);
        g_file_map[g_file_count].ss_index = ss_index;
        g_file_map[g_file_count].is_directory = false;
        g_file_map[g_file_count].acl_count = 0;
        g_file_map[g_file_count].word_count = 0;
        g_file_map[g_file_count].char_count = 0;

        time_t now = time(NULL);
        g_file_map[g_file_count].created_at = now;
        g_file_map[g_file_count].modified_at = now;
        g_file_map[g_file_count].accessed_at = now;
        
        trie_insert(filename, g_file_count);
        cache_add(filename, g_file_count);
          
        // Replication
        pthread_mutex_lock(&replica_mutex);
        if (g_file_replica_count < MAX_FILE_REPLICAS) {
            FileReplicationEntry* rep_entry = &g_file_replicas[g_file_replica_count];
            strcpy(rep_entry->path, filename);
            rep_entry->replica_ss_indices[0] = ss_index;
            rep_entry->replica_count = 1;
            
            int replicas_added = 0;
            // Note: loops g_ss_count_ext now for safety
            for (int attempt = 0; attempt < g_ss_count_ext && replicas_added < (REPLICATION_FACTOR - 1); attempt++) {
                int replica_ss = select_replica_ss(ss_index);
                
                if (replica_ss != -1 && replica_ss != ss_index) {
                    bool already_added = false;
                    for (int j = 1; j <= replicas_added; j++) {
                        if (rep_entry->replica_ss_indices[j] == replica_ss) {
                            already_added = true;
                            break;
                        }
                    }
                    
                    if (!already_added) {
                        rep_entry->replica_ss_indices[replicas_added + 1] = replica_ss;
                        rep_entry->replica_count++;
                        replicas_added++;
                        
                        enqueue_async_write(filename, "NM_CREATE", replica_ss);
                    }
                }
            }
            g_file_replica_count++;
        }
        pthread_mutex_unlock(&replica_mutex);
        
        g_file_count++;
        save_metadata_to_disk();
        
        pthread_mutex_unlock(&file_map_mutex);

        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_OK);
        send(client_fd, resp_buf, strlen(resp_buf), 0);

    } else {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
    }
}

void do_undo(int client_fd, char* username, char* filename) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: UNDO, FILE: %s", username, filename);
    log_event(log_msg);
    printf("Client %s requesting UNDO: %s\n", username, filename);

    char resp_buf[MAX_MSG_LEN];
    
    // 1. Lock the central mutex
    pthread_mutex_lock(&file_map_mutex);

    int file_index = -1;
    // char resp_buf[MAX_MSG_LEN]; // Make sure resp_buf is declared

    // 2. Check cache (fastest)
    CacheEntry* entry = cache_find(filename);
    
    if (entry) {
        // --- CACHE HIT ---
        file_index = entry->file_index;
        cache_move_to_front(entry); // Mark as recently used
    } else {
        // --- CACHE MISS ---
        // 3. Check trie (fast)
        file_index = trie_search(filename);
        if (file_index != -1) {
            // 4. Add to cache if we found it
            cache_add(filename, file_index);
        }
    }
    
    // 5. Handle "Not Found"
    if (file_index == -1) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_NOT_FOUND);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return; // Exit the function
    }
    
    // --- We now have a valid file_index, and the lock is still held ---
    FileMapEntry* file = &g_file_map[file_index];

    bool has_write_access = false;
    if (strcmp(username, file->owner) == 0) {
        has_write_access = true;
    }
    if (!has_write_access) {
        for (int i = 0; i < file->acl_count; i++) {
            if (strcmp(username, file->acl_list[i].username) == 0) {
                if (file->acl_list[i].permission == PERM_WRITE) {
                    has_write_access = true;
                }
                break;
            }
        }
    }
    if (!has_write_access) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_FORBIDDEN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }

    // 3. User has access. Get the SS info.
    int ss_index = g_file_map[file_index].ss_index;
    pthread_mutex_unlock(&file_map_mutex);

    // 4. Get the SS's COMMAND-LINE socket
    pthread_mutex_lock(&ss_list_mutex_ext);
    int ss_fd = g_ss_list_ext[ss_index].conn_fd; // <-- This is the important part
    pthread_mutex_unlock(&ss_list_mutex_ext);

    // 5. Send command to the SS
    char command_buf[MAX_MSG_LEN];
    sprintf(command_buf, "%s %s\n", NM_UNDO, filename);
    
    if (send(ss_fd, command_buf, strlen(command_buf), 0) < 0) {
        perror("Failed to send NM_UNDO to SS");
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        return;
    }

    // 6. Wait for ACK from SS
    char ss_resp[MAX_MSG_LEN];
    memset(ss_resp, 0, MAX_MSG_LEN);
    if (read(ss_fd, ss_resp, MAX_MSG_LEN - 1) <= 0) {
        printf("SS failed to respond to UNDO\n");
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        return;
    }
    

    // 7. Check SS response and commit metadata change
    if (strncmp(ss_resp, RESP_OK, strlen(RESP_OK)) == 0) {
        // The UNDO was successful, now we must update our metadata
        pthread_mutex_lock(&file_map_mutex);

        // We must re-find the file index, in case it changed
        // while we were talking to the SS.
        int current_file_index = trie_search(filename);
        
        if (current_file_index != -1) {
            FileMapEntry* file = &g_file_map[current_file_index];
            file->modified_at = time(NULL);
            save_metadata_to_disk(); // Save the new timestamp
        }
        // else: file was deleted while we were unlocked. Nothing to do.

        pthread_mutex_unlock(&file_map_mutex);
    }

    // 8. Relay SS response (e.g., "200" or "404") to the client
    send(client_fd, ss_resp, strlen(ss_resp), 0);

}

void do_read(int client_fd, char* username, char* filename) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: READ, FILE: %s", username, filename);
    log_event(log_msg);
    printf("Client %s requesting READ: %s\n", username, filename); // <-- Add this line back
    char resp_buf[MAX_MSG_LEN];
    
    // 1. Lock the central mutex
    pthread_mutex_lock(&file_map_mutex);

    int file_index = -1;
    // char resp_buf[MAX_MSG_LEN]; // Make sure resp_buf is declared

    // 2. Check cache (fastest)
    CacheEntry* entry = cache_find(filename);
    
    if (entry) {
        // --- CACHE HIT ---
        file_index = entry->file_index;
        cache_move_to_front(entry); // Mark as recently used
    } else {
        // --- CACHE MISS ---
        // 3. Check trie (fast)
        file_index = trie_search(filename);
        if (file_index != -1) {
            // 4. Add to cache if we found it
            cache_add(filename, file_index);
        }
    }
    
    // 5. Handle "Not Found"
    if (file_index == -1) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_NOT_FOUND);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return; // Exit the function
    }
    
    // --- We now have a valid file_index, and the lock is still held ---
    FileMapEntry* file = &g_file_map[file_index];

    // --- SAFETY CHECK ---
    if (file->is_directory) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s Cannot perform file operation on a folder\n", RESP_BAD_REQ);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    // --------------------

    file->accessed_at = time(NULL);

    bool has_access = false;

    // Check if the requester is the owner
    if (strcmp(username, file->owner) == 0) {
        has_access = true;
        printf("ACL Check: User %s is OWNER of %s. Access granted.\n", username, filename);
    }

    // If not owner, check the ACL list
    if (!has_access) {
        for (int i = 0; i < file->acl_count; i++) {
            // Check username AND if they have 'R' or 'W' permission
            if (strcmp(username, file->acl_list[i].username) == 0) {
                has_access = true;
                printf("ACL Check: User %s found in ACL for %s. Access granted.\n", username, filename);
                break;
            }
        }
    }

    // 3. ENFORCE
    if (!has_access) {
        printf("ACL Check: User %s has NO ACCESS to %s. Denying.\n", username, filename);
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_FORBIDDEN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }

    // 3. Get the SS's info
    int ss_index = g_file_map[file_index].ss_index;
    pthread_mutex_unlock(&file_map_mutex); // Done with file map

    // Check if primary SS is offline, use replica if available
    pthread_mutex_lock(&ss_list_mutex_ext);
    if (g_ss_list_ext[ss_index].status == SS_STATUS_OFFLINE) {
        printf("[FAILOVER] Primary SS[%d] is offline for '%s', checking replicas...\n", ss_index, filename);
        
        // Find a healthy replica
        pthread_mutex_lock(&replica_mutex);
        for (int i = 0; i < g_file_replica_count; i++) {
            if (strcmp(g_file_replicas[i].path, filename) == 0) {
                // Found the file's replica info
                for (int j = 0; j < g_file_replicas[i].replica_count; j++) {
                    int replica_idx = g_file_replicas[i].replica_ss_indices[j];
                    if (g_ss_list_ext[replica_idx].status == SS_STATUS_ONLINE) {
                        ss_index = replica_idx;
                        printf("[FAILOVER] Using replica SS[%d] for READ of '%s'\n", ss_index, filename);
                        break;
                    }
                }
                break;
            }
        }
        pthread_mutex_unlock(&replica_mutex);
        
        // If still offline, no replicas available
        if (g_ss_list_ext[ss_index].status == SS_STATUS_OFFLINE) {
            pthread_mutex_unlock(&ss_list_mutex_ext);
            snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SS_DOWN);
            send(client_fd, resp_buf, strlen(resp_buf), 0);
            return;
        }
    }
    pthread_mutex_unlock(&ss_list_mutex_ext);

// 4. Get SS client-facing IP/port
    pthread_mutex_lock(&ss_list_mutex_ext);    
    char ss_ip[INET_ADDRSTRLEN];
    int ss_port = g_ss_list_ext[ss_index].client_port; 
    strcpy(ss_ip, g_ss_list_ext[ss_index].ip);         
    pthread_mutex_unlock(&ss_list_mutex_ext);      

    // 5. Send SS info to client for direct connection
    snprintf(resp_buf, MAX_MSG_LEN, "%s %s %d\n", RESP_SS_INFO, ss_ip, ss_port);
    send(client_fd, resp_buf, strlen(resp_buf), 0);
}

void do_write(int client_fd, char* username, char* filename) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: WRITE, FILE: %s", username, filename);
    log_event(log_msg);
    printf("Client %s requesting WRITE: %s\n", username, filename);
    char resp_buf[MAX_MSG_LEN];
    
    // 1. Lock the central mutex
    pthread_mutex_lock(&file_map_mutex);

    int file_index = -1;
    // char resp_buf[MAX_MSG_LEN]; // Make sure resp_buf is declared

    // 2. Check cache (fastest)
    CacheEntry* entry = cache_find(filename);
    
    if (entry) {
        // --- CACHE HIT ---
        file_index = entry->file_index;
        cache_move_to_front(entry); // Mark as recently used
    } else {
        // --- CACHE MISS ---
        // 3. Check trie (fast)
        file_index = trie_search(filename);
        if (file_index != -1) {
            // 4. Add to cache if we found it
            cache_add(filename, file_index);
        }
    }
    
    // 5. Handle "Not Found"
    if (file_index == -1) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_NOT_FOUND);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return; // Exit the function
    }
    
    // --- We now have a valid file_index, and the lock is still held ---
    FileMapEntry* file = &g_file_map[file_index];

    // --- SAFETY CHECK ---
    if (file->is_directory) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s Cannot perform file operation on a folder\n", RESP_BAD_REQ);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    // --------------------

    file->accessed_at = time(NULL);

    bool has_write_access = false;

    // --- Stricter ACL Check for WRITE ---
    // 1. Check if owner
    if (strcmp(username, file->owner) == 0) {
        has_write_access = true;
        printf("ACL Check: User %s is OWNER of %s. Write access granted.\n", username, filename);
    }

    // 2. If not owner, check ACL for 'W' permission
    if (!has_write_access) {
        for (int i = 0; i < file->acl_count; i++) {
            if (strcmp(username, file->acl_list[i].username) == 0) {
                // MUST have 'W' (PERM_WRITE)
                if (file->acl_list[i].permission == PERM_WRITE) {
                    has_write_access = true;
                    printf("ACL Check: User %s found in ACL with 'W' perm for %s. Write access granted.\n", username, filename);
                }
                break; // Found user, no need to search more
            }
        }
    }
    // --- End of ACL Check ---

    // 3. ENFORCE
    if (!has_write_access) {
        printf("ACL Check: User %s has NO WRITE ACCESS to %s. Denying.\n", username, filename);
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_FORBIDDEN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }

    // 4. Get the SS's info (User has access, proceed same as do_read)
    int ss_index = g_file_map[file_index].ss_index;
    pthread_mutex_unlock(&file_map_mutex); // Done with file map

    // Check if primary SS is offline, use replica if available
    pthread_mutex_lock(&ss_list_mutex_ext);
    if (g_ss_list_ext[ss_index].status == SS_STATUS_OFFLINE) {
        printf("[FAILOVER] Primary SS[%d] is offline for '%s', checking replicas...\n", ss_index, filename);
        
        // Find a healthy replica
        pthread_mutex_lock(&replica_mutex);
        for (int i = 0; i < g_file_replica_count; i++) {
            if (strcmp(g_file_replicas[i].path, filename) == 0) {
                // Found the file's replica info
                for (int j = 0; j < g_file_replicas[i].replica_count; j++) {
                    int replica_idx = g_file_replicas[i].replica_ss_indices[j];
                    if (g_ss_list_ext[replica_idx].status == SS_STATUS_ONLINE) {
                        ss_index = replica_idx;
                        printf("[FAILOVER] Using replica SS[%d] for WRITE of '%s'\n", ss_index, filename);
                        break;
                    }
                }
                break;
            }
        }
        pthread_mutex_unlock(&replica_mutex);
        
        // If still offline, no replicas available
        if (g_ss_list_ext[ss_index].status == SS_STATUS_OFFLINE) {
            pthread_mutex_unlock(&ss_list_mutex_ext);
            snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SS_DOWN);
            send(client_fd, resp_buf, strlen(resp_buf), 0);
            return;
        }
    }
    pthread_mutex_unlock(&ss_list_mutex_ext);

    // 5. Get SS client-facing IP/port
    pthread_mutex_lock(&ss_list_mutex_ext);        
    char ss_ip[INET_ADDRSTRLEN];
    int ss_port = g_ss_list_ext[ss_index].client_port; 
    strcpy(ss_ip, g_ss_list_ext[ss_index].ip);         
    pthread_mutex_unlock(&ss_list_mutex_ext);      
    // 6. Send the referral to the client
    sprintf(resp_buf, "%s %s %d\n", RESP_SS_INFO, ss_ip, ss_port);
    send(client_fd, resp_buf, strlen(resp_buf), 0);
}

void* handle_connection(void *arg){
    int conn_fd = *((int*)arg);
    char buffer[MAX_MSG_LEN];
    memset(buffer, 0, MAX_MSG_LEN);

    if (read(conn_fd, buffer, MAX_MSG_LEN - 1) <= 0) {
        printf("Handshake failed. Closing connection.\n");
        close(conn_fd);
        free(arg); // Don't forget to free arg!
        return NULL;
    }

    printf("Handshake received: %s\n", buffer);

    if (strncmp(buffer, C_INIT, strlen(C_INIT)) == 0) {
        // ... Client logic (Same as before) ...
        char username[MAX_USERNAME_LEN];
        sscanf(buffer, "%*s %s", username);
        
        pthread_mutex_lock(&registry_mutex);
        registry_add_user(username);
        pthread_mutex_unlock(&registry_mutex);
                
        pthread_mutex_lock(&client_list_mutex);
        g_client_list[g_client_count].conn_fd = conn_fd;
        strcpy(g_client_list[g_client_count].username, username);
        g_client_count++;
        pthread_mutex_unlock(&client_list_mutex);
        
        char log_msg[MAX_MSG_LEN];
        snprintf(log_msg, MAX_MSG_LEN, "New connection: CLIENT, USER: %s", username);
        log_event(log_msg);
        send(conn_fd, RESP_OK "\n", strlen(RESP_OK "\n"), 0);
        
        handle_client_commands(arg);
        
    } else if (strncmp(buffer, S_INIT, strlen(S_INIT)) == 0) {
        char ip[INET_ADDRSTRLEN];
        int client_port;
        sscanf(buffer, "%*s %s %*d %d", ip, &client_port); 
        
        // --- LEGACY LIST LOGIC REMOVED ---
        
        bool is_recovery = false;
        int recovery_index = -1;
        
        pthread_mutex_lock(&ss_list_mutex_ext);
        for (int i = 0; i < g_ss_count_ext; i++) {
            if (strcmp(g_ss_list_ext[i].ip, ip) == 0 && 
                g_ss_list_ext[i].client_port == client_port &&
                g_ss_list_ext[i].status == SS_STATUS_OFFLINE) {
                is_recovery = true;
                recovery_index = i;
                g_ss_list_ext[i].conn_fd = conn_fd; // Update FD
                g_ss_list_ext[i].status = SS_STATUS_RECOVERING;
                break;
            }
        }
        
        int new_ss_index = g_ss_count_ext;
        if (!is_recovery) {
            if (g_ss_count_ext < MAX_SS) {
                g_ss_list_ext[new_ss_index].conn_fd = conn_fd;
                strcpy(g_ss_list_ext[new_ss_index].ip, ip);
                g_ss_list_ext[new_ss_index].client_port = client_port;
                g_ss_list_ext[new_ss_index].status = SS_STATUS_ONLINE;
                g_ss_list_ext[new_ss_index].last_heartbeat = time(NULL);
                g_ss_list_ext[new_ss_index].pending_write_count = 0;
                g_ss_count_ext++;
            }
        }
        pthread_mutex_unlock(&ss_list_mutex_ext);
        
        if (is_recovery) {
            printf("[RECOVERY] Storage Server %s:%d reconnected (index=%d)\n", ip, client_port, recovery_index);
        } else {
            printf("[HEARTBEAT] Registered new Storage Server at %s:%d (index=%d)\n", ip, client_port, new_ss_index);
        }
        
        send(conn_fd, RESP_OK "\n", strlen(RESP_OK "\n"), 0);
        
        if (is_recovery) {
            pthread_t recovery_tid;
            int* idx = malloc(sizeof(int));
            *idx = recovery_index;
            if (pthread_create(&recovery_tid, NULL, handle_ss_recovery, idx) == 0) {
                pthread_detach(recovery_tid);
            }
        }
        free(arg);
    } 
    else if (strncmp(buffer, S_META_UPDATE, strlen(S_META_UPDATE)) == 0) {
        // Format sent by SS: "S_META_UPDATE <ss_port> <filename> <word_count> <char_count>"
        int ss_port;
        char filename[MAX_PATH_LEN];
        int wc = 0;
        int cc = 0;

        // 1. Parse the message
        // We use %d for port, %s for filename, %d for words, %d for chars
        if (sscanf(buffer, "%*s %d %s %d %d", &ss_port, filename, &wc, &cc) == 4) {
            
            char log_msg[MAX_MSG_LEN];
            snprintf(log_msg, MAX_MSG_LEN, "META_UPDATE for %s: Words=%d, Chars=%d", filename, wc, cc);
            log_event(log_msg);

            // 2. Update the global map safely
            pthread_mutex_lock(&file_map_mutex);
            
            int file_index = trie_search(filename);
            if (file_index != -1) {
                g_file_map[file_index].word_count = wc;
                g_file_map[file_index].char_count = cc;
                // Optionally update modified time here too if you want strict accuracy
                g_file_map[file_index].modified_at = time(NULL);
                
                // Save to disk so updates persist across NM restarts
                save_metadata_to_disk();
                printf("[NM] Updated metadata for %s (W:%d C:%d)\n", filename, wc, cc);
            } else {
                printf("[NM] Warning: Received metadata update for unknown file %s\n", filename);
            }
            
            pthread_mutex_unlock(&file_map_mutex);
        } else {
            printf("[NM] Error: Malformed S_META_UPDATE message: %s\n", buffer);
        }

        // 3. Clean up
        close(conn_fd);
        free(arg);
        return NULL;
    }
    else {
        printf("Unknown handshake.\n");
        close(conn_fd);
        free(arg);
    }
    return NULL;
}

void do_add_access(int client_fd, char* requester_username, char* filename, char* target_user, char permission_flag) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: ADDACCESS, FILE: %s, TARGET: %s, PERM: %c", requester_username, filename, target_user, permission_flag);
    log_event(log_msg);

    char resp_buf[MAX_MSG_LEN];
    
    // 1. Lock the central mutex
    pthread_mutex_lock(&file_map_mutex);

    int file_index = -1;
    // char resp_buf[MAX_MSG_LEN]; // Make sure resp_buf is declared

    // 2. Check cache (fastest)
    CacheEntry* entry = cache_find(filename);
    
    if (entry) {
        // --- CACHE HIT ---
        file_index = entry->file_index;
        cache_move_to_front(entry); // Mark as recently used
    } else {
        // --- CACHE MISS ---
        // 3. Check trie (fast)
        file_index = trie_search(filename);
        if (file_index != -1) {
            // 4. Add to cache if we found it
            cache_add(filename, file_index);
        }
    }
    
    // 5. Handle "Not Found"
    if (file_index == -1) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_NOT_FOUND);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return; // Exit the function
    }
    
    // --- We now have a valid file_index, and the lock is still held ---
    FileMapEntry* file = &g_file_map[file_index];

    if (strcmp(requester_username, file->owner) != 0) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_FORBIDDEN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }

    // 3. Add or update the permission
    bool user_found = false;
    for (int i = 0; i < file->acl_count; i++) {
        if (strcmp(file->acl_list[i].username, target_user) == 0) {
            // User already in list, just update permission
            file->acl_list[i].permission = permission_flag;
            user_found = true;
            break;
        }
    }

    if (!user_found) {
        // User not in list, add new entry (if space is available)
        if (file->acl_count < MAX_CLIENTS) {
            strcpy(file->acl_list[file->acl_count].username, target_user);
            file->acl_list[file->acl_count].permission = permission_flag;
            file->acl_count++;
        } else {
            // Handle case where ACL list is full (optional)
        }
    }

    // 4. Send success
    printf("Client %s granted %c access for %s to %s\n", requester_username, permission_flag, filename, target_user);
    snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_OK);
    send(client_fd, resp_buf, strlen(resp_buf), 0);
    save_metadata_to_disk();
    pthread_mutex_unlock(&file_map_mutex);
}

void do_rem_access(int client_fd, char* requester_username, char* filename, char* target_user) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: REMACCESS, FILE: %s, TARGET: %s", requester_username, filename, target_user);
    log_event(log_msg);
    printf("Client %s requesting REMACCESS for %s from %s\n", requester_username, target_user, filename);

    char resp_buf[MAX_MSG_LEN];
    
    // 1. Lock the central mutex
    pthread_mutex_lock(&file_map_mutex);

    int file_index = -1;
    // char resp_buf[MAX_MSG_LEN]; // Make sure resp_buf is declared

    // 2. Check cache (fastest)
    CacheEntry* entry = cache_find(filename);
    
    if (entry) {
        // --- CACHE HIT ---
        file_index = entry->file_index;
        cache_move_to_front(entry); // Mark as recently used
    } else {
        // --- CACHE MISS ---
        // 3. Check trie (fast)
        file_index = trie_search(filename);
        if (file_index != -1) {
            // 4. Add to cache if we found it
            cache_add(filename, file_index);
        }
    }
    
    // 5. Handle "Not Found"
    if (file_index == -1) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_NOT_FOUND);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return; // Exit the function
    }
    
    // --- We now have a valid file_index, and the lock is still held ---
    FileMapEntry* file = &g_file_map[file_index];
    
    if (strcmp(requester_username, file->owner) != 0) {
        printf("ACL Check: User %s is NOT OWNER of %s. RemAccess denied.\n", requester_username, filename);
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_FORBIDDEN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }

    // 3. Find the user in the ACL list
    int user_index = -1;
    for (int i = 0; i < file->acl_count; i++) {
        if (strcmp(file->acl_list[i].username, target_user) == 0) {
            user_index = i;
            break;
        }
    }

    // 4. If found, remove them using "swap-with-last"
    if (user_index != -1) {
        printf("Removing %s from ACL for %s\n", target_user, filename);
        // Copy the last element over the one we're deleting
        file->acl_list[user_index] = file->acl_list[file->acl_count - 1];
        file->acl_count--; // Decrease the count
    } else {
        // User wasn't in the list anyway, but that's not an error.
        printf("User %s was not in ACL for %s. No action taken.\n", target_user, filename);
    }

    // 5. Send success
    snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_OK);
    send(client_fd, resp_buf, strlen(resp_buf), 0);
    save_metadata_to_disk();
    pthread_mutex_unlock(&file_map_mutex);
}

/*
 * Handle: C_REQ_ACC <filename> <-R|-W>
 * User requests READ or WRITE access to a file they don't own
 */
void do_request_access(int client_fd, char* requester_username, char* filename, char permission_flag) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: REQUEST_ACCESS, FILE: %s, PERM: %c", 
             requester_username, filename, permission_flag);
    log_event(log_msg);
    printf("Client %s requesting access to %s (perm: %c)\n", requester_username, filename, permission_flag);

    char resp_buf[MAX_MSG_LEN];
    
    // 1. Check if file exists
    pthread_mutex_lock(&file_map_mutex);
    
    int file_index = trie_search(filename);
    if (file_index == -1) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s File not found\n", RESP_NOT_FOUND);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    FileMapEntry* file = &g_file_map[file_index];
    
    // 2. Check if requester is already the owner
    if (strcmp(requester_username, file->owner) == 0) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s You are already the owner\n", RESP_BAD_REQ);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    // 3. Check if requester already has access
    for (int i = 0; i < file->acl_count; i++) {
        if (strcmp(requester_username, file->acl_list[i].username) == 0) {
            // If they already have Write, or if they have Read and are asking for Read -> Reject
            if (file->acl_list[i].permission == 'W' || 
               (file->acl_list[i].permission == 'R' && permission_flag == 'R')) {
                
                snprintf(resp_buf, MAX_MSG_LEN, "%s You already have sufficient access to this file\n", RESP_CONFLICT);
                send(client_fd, resp_buf, strlen(resp_buf), 0);
                pthread_mutex_unlock(&file_map_mutex);
                return;
            }
            // If they have 'R' and are asking for 'W', allow loop to finish so we can create a request
        }
    }
    
    pthread_mutex_unlock(&file_map_mutex);
    
    // 4. Check if there's already a pending request
    pthread_mutex_lock(&access_request_mutex);
    
    for (int i = 0; i < g_access_request_count; i++) {
        if (strcmp(g_access_requests[i].requester, requester_username) == 0 &&
            strcmp(g_access_requests[i].filename, filename) == 0 &&
            g_access_requests[i].status == REQ_STATUS_PENDING) {
            snprintf(resp_buf, MAX_MSG_LEN, "%s You already have a pending request for this file\n", RESP_CONFLICT);
            send(client_fd, resp_buf, strlen(resp_buf), 0);
            pthread_mutex_unlock(&access_request_mutex);
            return;
        }
    }
    
    // 5. Create new access request
    if (g_access_request_count >= MAX_ACCESS_REQUESTS) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s Request queue is full\n", RESP_SRV_ERR);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&access_request_mutex);
        return;
    }
    
    AccessRequest* new_req = &g_access_requests[g_access_request_count];
    new_req->request_id = g_next_request_id++;
    strcpy(new_req->requester, requester_username);
    strcpy(new_req->filename, filename);
    new_req->permission = permission_flag;
    new_req->status = REQ_STATUS_PENDING;
    new_req->requested_at = time(NULL);
    new_req->processed_at = 0;
    
    g_access_request_count++;
    
    pthread_mutex_unlock(&access_request_mutex);
    
    // 6. Send success
    snprintf(resp_buf, MAX_MSG_LEN, "%s Request submitted (ID: %d)\n", RESP_OK, new_req->request_id);
    send(client_fd, resp_buf, strlen(resp_buf), 0);
    printf("Access request created: ID=%d, User=%s, File=%s\n", 
           new_req->request_id, requester_username, filename);
}

/*
 * Handle: C_VIEW_REQ <filename>
 * File owner views pending requests for their file
 */
void do_view_requests(int client_fd, char* requester_username, char* filename) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: VIEW_REQUESTS, FILE: %s", requester_username, filename);
    log_event(log_msg);
    
    char resp_buf[4096];
    char payload[4096] = "";
    
    // 1. Check if file exists and if requester is owner
    pthread_mutex_lock(&file_map_mutex);
    
    int file_index = trie_search(filename);
    if (file_index == -1) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s File not found\n", RESP_NOT_FOUND);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    FileMapEntry* file = &g_file_map[file_index];
    
    if (strcmp(requester_username, file->owner) != 0) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s Only the file owner can view requests\n", RESP_FORBIDDEN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    pthread_mutex_unlock(&file_map_mutex);
    
    // 2. Build list of pending requests for this file
    snprintf(payload, sizeof(payload), "%s\n=== Pending Access Requests for %s ===\n", RESP_OK, filename);
    
    pthread_mutex_lock(&access_request_mutex);
    
    int found_count = 0;
    for (int i = 0; i < g_access_request_count; i++) {
        if (strcmp(g_access_requests[i].filename, filename) == 0 &&
            g_access_requests[i].status == REQ_STATUS_PENDING) {
            
            char time_str[100];
            struct tm ltime;
            localtime_r(&g_access_requests[i].requested_at, &ltime);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &ltime);
            
            snprintf(payload + strlen(payload), sizeof(payload) - strlen(payload),
                     "ID: %d | User: %s | Permission: %c | Requested: %s\n",
                     g_access_requests[i].request_id,
                     g_access_requests[i].requester,
                     g_access_requests[i].permission,
                     time_str);
            found_count++;
        }
    }
    
    pthread_mutex_unlock(&access_request_mutex);
    
    if (found_count == 0) {
        snprintf(payload + strlen(payload), sizeof(payload) - strlen(payload), 
                 "No pending requests.\n");
    }
    
    send(client_fd, payload, strlen(payload), 0);
}

/*
 * Handle: C_APPROVE <request_id>
 * File owner approves a pending access request
 */
void do_approve_request(int client_fd, char* requester_username, int request_id) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: APPROVE, REQ_ID: %d", requester_username, request_id);
    log_event(log_msg);
    
    char resp_buf[MAX_MSG_LEN];
    
    pthread_mutex_lock(&access_request_mutex);
    
    // 1. Find the request
    AccessRequest* req = NULL;
    for (int i = 0; i < g_access_request_count; i++) {
        if (g_access_requests[i].request_id == request_id) {
            req = &g_access_requests[i];
            break;
        }
    }
    
    if (req == NULL) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s Request not found\n", RESP_NOT_FOUND);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&access_request_mutex);
        return;
    }
    
    // 2. Check if already processed
    if (req->status != REQ_STATUS_PENDING) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s Request already processed\n", RESP_BAD_REQ);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&access_request_mutex);
        return;
    }
    
    // Store request details before unlocking
    char req_filename[MAX_PATH_LEN];
    char req_requester[MAX_USERNAME_LEN];
    char req_permission = req->permission;
    strcpy(req_filename, req->filename);
    strcpy(req_requester, req->requester);
    
    pthread_mutex_unlock(&access_request_mutex);
    
    // 3. Verify requester is the file owner
    pthread_mutex_lock(&file_map_mutex);
    
    int file_index = trie_search(req_filename);
    if (file_index == -1) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s File no longer exists\n", RESP_NOT_FOUND);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    FileMapEntry* file = &g_file_map[file_index];
    
    if (strcmp(requester_username, file->owner) != 0) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s Only the file owner can approve requests\n", RESP_FORBIDDEN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    // 4. Add user to ACL
    bool user_found = false;
    for (int i = 0; i < file->acl_count; i++) {
        if (strcmp(file->acl_list[i].username, req_requester) == 0) {
            file->acl_list[i].permission = req_permission;
            user_found = true;
            break;
        }
    }
    
    if (!user_found) {
        if (file->acl_count < MAX_CLIENTS) {
            strcpy(file->acl_list[file->acl_count].username, req_requester);
            file->acl_list[file->acl_count].permission = req_permission;
            file->acl_count++;
        } else {
            snprintf(resp_buf, MAX_MSG_LEN, "%s ACL is full\n", RESP_SRV_ERR);
            send(client_fd, resp_buf, strlen(resp_buf), 0);
            pthread_mutex_unlock(&file_map_mutex);
            return;
        }
    }
    
    save_metadata_to_disk();
    pthread_mutex_unlock(&file_map_mutex);
    
    // 5. Mark request as approved
    pthread_mutex_lock(&access_request_mutex);
    req->status = REQ_STATUS_APPROVED;
    req->processed_at = time(NULL);
    pthread_mutex_unlock(&access_request_mutex);
    
    // 6. Send success
    snprintf(resp_buf, MAX_MSG_LEN, "%s Access granted to %s\n", RESP_OK, req_requester);
    send(client_fd, resp_buf, strlen(resp_buf), 0);
    printf("Request %d approved by %s\n", request_id, requester_username);
}

/*
 * Handle: C_DENY <request_id>
 * File owner denies a pending access request
 */
void do_deny_request(int client_fd, char* requester_username, int request_id) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: DENY, REQ_ID: %d", requester_username, request_id);
    log_event(log_msg);
    
    char resp_buf[MAX_MSG_LEN];
    
    pthread_mutex_lock(&access_request_mutex);
    
    // 1. Find the request
    AccessRequest* req = NULL;
    for (int i = 0; i < g_access_request_count; i++) {
        if (g_access_requests[i].request_id == request_id) {
            req = &g_access_requests[i];
            break;
        }
    }
    
    if (req == NULL) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s Request not found\n", RESP_NOT_FOUND);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&access_request_mutex);
        return;
    }
    
    // 2. Check if already processed
    if (req->status != REQ_STATUS_PENDING) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s Request already processed\n", RESP_BAD_REQ);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&access_request_mutex);
        return;
    }
    
    // Store filename for ownership check
    char req_filename[MAX_PATH_LEN];
    strcpy(req_filename, req->filename);
    
    pthread_mutex_unlock(&access_request_mutex);
    
    // 3. Verify requester is the file owner
    pthread_mutex_lock(&file_map_mutex);
    
    int file_index = trie_search(req_filename);
    if (file_index == -1) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s File no longer exists\n", RESP_NOT_FOUND);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    FileMapEntry* file = &g_file_map[file_index];
    
    if (strcmp(requester_username, file->owner) != 0) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s Only the file owner can deny requests\n", RESP_FORBIDDEN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    pthread_mutex_unlock(&file_map_mutex);
    
    // 4. Mark request as denied
    pthread_mutex_lock(&access_request_mutex);
    req->status = REQ_STATUS_DENIED;
    req->processed_at = time(NULL);
    pthread_mutex_unlock(&access_request_mutex);
    
    // 5. Send success
    snprintf(resp_buf, MAX_MSG_LEN, "%s Request denied\n", RESP_OK);
    send(client_fd, resp_buf, strlen(resp_buf), 0);
    printf("Request %d denied by %s\n", request_id, requester_username);
}

/*
 * Handle: C_MY_REQ
 * User views their own access requests (pending, approved, denied)
 */
void do_my_requests(int client_fd, char* requester_username) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: MY_REQUESTS", requester_username);
    log_event(log_msg);
    
    char payload[4096] = "";
    snprintf(payload, sizeof(payload), "%s\n=== Your Access Requests ===\n", RESP_OK);
    
    pthread_mutex_lock(&access_request_mutex);
    
    int found_count = 0;
    for (int i = 0; i < g_access_request_count; i++) {
        if (strcmp(g_access_requests[i].requester, requester_username) == 0) {
            char time_str[100];
            struct tm ltime;
            localtime_r(&g_access_requests[i].requested_at, &ltime);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &ltime);
            
            char status_str[20];
            if (g_access_requests[i].status == REQ_STATUS_PENDING) {
                strcpy(status_str, "PENDING");
            } else if (g_access_requests[i].status == REQ_STATUS_APPROVED) {
                strcpy(status_str, "APPROVED");
            } else {
                strcpy(status_str, "DENIED");
            }
            
            snprintf(payload + strlen(payload), sizeof(payload) - strlen(payload),
                     "ID: %d | File: %s | Perm: %c | Status: %s | Date: %s\n",
                     g_access_requests[i].request_id,
                     g_access_requests[i].filename,
                     g_access_requests[i].permission,
                     status_str,
                     time_str);
            found_count++;
        }
    }
    
    pthread_mutex_unlock(&access_request_mutex);
    
    if (found_count == 0) {
        snprintf(payload + strlen(payload), sizeof(payload) - strlen(payload), 
                 "No requests found.\n");
    }
    
    send(client_fd, payload, strlen(payload), 0);
}

void do_create_folder(int client_fd, char* username, char* foldername) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: CREATEFOLDER, NAME: %s", username, foldername);
    log_event(log_msg);

    char resp_buf[MAX_MSG_LEN];

    pthread_mutex_lock(&file_map_mutex);
    
    // 1. Check existence
    if (trie_search(foldername) != -1) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s Folder/File already exists\n", RESP_CONFLICT);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }

    // 2. Create Metadata Entry
    strcpy(g_file_map[g_file_count].path, foldername);
    strcpy(g_file_map[g_file_count].owner, username);
    g_file_map[g_file_count].ss_index = -1; // No SS for folders
    g_file_map[g_file_count].is_directory = true; // <--- MARK AS FOLDER
    g_file_map[g_file_count].acl_count = 0;
    
    time_t now = time(NULL);
    g_file_map[g_file_count].created_at = now;
    g_file_map[g_file_count].modified_at = now;
    
    trie_insert(foldername, g_file_count);
    g_file_count++;
    save_metadata_to_disk();

    pthread_mutex_unlock(&file_map_mutex);

    snprintf(resp_buf, MAX_MSG_LEN, "%s Folder created\n", RESP_OK);
    send(client_fd, resp_buf, strlen(resp_buf), 0);
}

void do_move(int client_fd, char* username, char* filename, char* dest_folder) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: MOVE, FILE: %s, DEST: %s", username, filename, dest_folder);
    log_event(log_msg);

    char resp_buf[MAX_MSG_LEN];

    // Lock GLOBAL mutex to prevent race conditions during the complex move
    pthread_mutex_lock(&file_map_mutex);
    
    // 1. Find Source
    int file_index = trie_search(filename);
    if (file_index == -1) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s Source file not found\n", RESP_NOT_FOUND);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    // 2. Check permissions
    if (strcmp(g_file_map[file_index].owner, username) != 0) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s Permission denied\n", RESP_FORBIDDEN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }

    // 3. Reject Folder Move (For Correctness/Simplicity)
    if (g_file_map[file_index].is_directory) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s Moving folders is not supported\n", RESP_BAD_REQ);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }

    // 4. Construct New Path
    char new_path[MAX_PATH_LEN];
    
    // Handle "." as destination (move to root)
    if (strcmp(dest_folder, ".") == 0) {
        // If dest is ".", strip directory prefix from filename. 
        // E.g. "a/b.txt" -> "b.txt"
        char *base_name = strrchr(filename, '/');
        if (base_name) {
            strcpy(new_path, base_name + 1); // +1 skips '/'
        } else {
            // Already at root
            strcpy(new_path, filename);
        }
    } else {
        // Move into folder: "dest/filename_base"
        char *base_name = strrchr(filename, '/');
        if (base_name) {
            snprintf(new_path, MAX_PATH_LEN, "%s/%s", dest_folder, base_name + 1);
        } else {
            snprintf(new_path, MAX_PATH_LEN, "%s/%s", dest_folder, filename);
        }
    }
    
    // 5. Check Collision
    if (trie_search(new_path) != -1) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s Destination already exists\n", RESP_CONFLICT);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }

    // 6. SYNCHRONOUS SS UPDATE
    // We talk to the SS while holding the lock. 
    // This blocks other metadata ops, ensuring safety.
    int ss_index = g_file_map[file_index].ss_index;
    
    pthread_mutex_lock(&ss_list_mutex_ext);
    int ss_fd = g_ss_list_ext[ss_index].conn_fd;
    pthread_mutex_unlock(&ss_list_mutex_ext);

    char cmd_buf[MAX_MSG_LEN];
    snprintf(cmd_buf, MAX_MSG_LEN, "%s %s %s\n", NM_RENAME, filename, new_path);
    
    // Send
    if (send(ss_fd, cmd_buf, strlen(cmd_buf), 0) < 0) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }

    // Wait for ACK (This prevents the race condition)
    char ss_resp[MAX_MSG_LEN];
    memset(ss_resp, 0, MAX_MSG_LEN);
    if (read(ss_fd, ss_resp, MAX_MSG_LEN - 1) <= 0) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    // Check ACK
    if (strncmp(ss_resp, RESP_OK, 3) != 0) {
        // SS failed (e.g., disk error). Do NOT update metadata.
        send(client_fd, ss_resp, strlen(ss_resp), 0); 
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }

    // 7. SS Succeeded. Update Metadata safely.
    trie_remove(filename);
    cache_remove(filename);
    
    strcpy(g_file_map[file_index].path, new_path);
    trie_insert(new_path, file_index);
    save_metadata_to_disk();
    
    pthread_mutex_unlock(&file_map_mutex);

    snprintf(resp_buf, MAX_MSG_LEN, "%s File moved\n", RESP_OK);
    send(client_fd, resp_buf, strlen(resp_buf), 0);
}

void do_view_folder(int client_fd, char* username, char* foldername) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: VIEWFOLDER, FOLDER: %s", username, foldername);
    log_event(log_msg);

    char resp_buf[MAX_MSG_LEN];
    char payload[4096] = "";

    pthread_mutex_lock(&file_map_mutex);
    
    // Check if the user is asking for the root directory (e.g., ".", "/" or empty)
    bool is_root = (strcmp(foldername, ".") == 0 || strcmp(foldername, "/") == 0 || strcmp(foldername, "") == 0);

    // 1. Validate folder existence if not root
    if (!is_root) {
        int folder_idx = trie_search(foldername);
        if (folder_idx == -1 || !g_file_map[folder_idx].is_directory) {
            snprintf(resp_buf, MAX_MSG_LEN, "%s Folder not found or not a folder\n", RESP_NOT_FOUND);
            send(client_fd, resp_buf, strlen(resp_buf), 0);
            pthread_mutex_unlock(&file_map_mutex);
            return;
        }
    }
    
    snprintf(payload, sizeof(payload), "%s\n=== Contents of %s ===\n", RESP_OK, foldername);

    int len = strlen(foldername);
    int count = 0;

    for(int i=0; i<g_file_count; i++) {
        const char* current_path = g_file_map[i].path;
        
        // Skip the folder itself (e.g., skip "JSP" when listing "JSP")
        if (strcmp(current_path, foldername) == 0) continue;
        
        if (is_root) {
            // Case 1: Root Folder Listing (e.g., VIEWFOLDER .)
            // We only want paths that contain NO slash.
            if (strchr(current_path, '/') == NULL) {
                 char type_char = g_file_map[i].is_directory ? 'D' : 'F';
                 snprintf(payload + strlen(payload), sizeof(payload) - strlen(payload), 
                          "[%c] %s\n", type_char, current_path);
                 count++;
            }
        } else {
            // Case 2: Sub-folder Listing (e.g., VIEWFOLDER JSP/J1)
            
            // Check if path starts with "foldername/"
            if (strncmp(current_path, foldername, len) == 0) {
                
                // Ensure it's prefixed by the folder and a slash
                if (current_path[len] == '/') {
                     char* relative_name = (char*)current_path + len + 1; // Path after "foldername/"
                     
                     // CRITICAL: Check if the remaining path contains another slash ('/')
                     if (strchr(relative_name, '/') == NULL) {
                         // This is a direct child (e.g., "JSP/J1/abc2.txt" -> "abc2.txt")
                         char type_char = g_file_map[i].is_directory ? 'D' : 'F';
                         snprintf(payload + strlen(payload), sizeof(payload) - strlen(payload), 
                                  "[%c] %s\n", type_char, relative_name);
                         count++;
                     }
                }
            }
        }
    }
    pthread_mutex_unlock(&file_map_mutex);

    if (count == 0) strcat(payload, "(Empty)\n");
    send(client_fd, payload, strlen(payload), 0);
}

void do_delete(int client_fd, char* requester_username, char* filename) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: DELETE, FILE: %s", requester_username, filename);
    log_event(log_msg);
    printf("Client %s requesting DELETE: %s\n", requester_username, filename);

    char resp_buf[MAX_MSG_LEN]; // Declared once

    // --- 1. Find file, check ACL, get SS index ---
    pthread_mutex_lock(&file_map_mutex);

    int file_index = -1;

    // 2. Check cache (fastest)
    CacheEntry* entry = cache_find(filename);
    
    if (entry) {
        file_index = entry->file_index;
        cache_move_to_front(entry);
    } else {
        file_index = trie_search(filename);
        if (file_index != -1) {
            cache_add(filename, file_index);
        }
    }
    
    // 5. Handle "Not Found"
    if (file_index == -1) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_NOT_FOUND);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    FileMapEntry* file = &g_file_map[file_index];
    
    // 6. ACL Check
    if (strcmp(requester_username, file->owner) != 0) {
        printf("ACL Check: User %s is NOT OWNER of %s. Delete denied.\n", requester_username, filename);
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_FORBIDDEN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }

    // 7. Get the SS index *before* unlocking
    int ss_index = file->ss_index;

    // 8. *** UNLOCK THE MUTEX *** before network I/O
    pthread_mutex_unlock(&file_map_mutex);

    // --- 2. Communicate with SS (No lock held) ---

    // 9. Get SS info
    pthread_mutex_lock(&ss_list_mutex_ext);
    int ss_fd = g_ss_list_ext[ss_index].conn_fd;
    pthread_mutex_unlock(&ss_list_mutex_ext);

    // 10. Send command to SS
    char command_buf[MAX_MSG_LEN];
    sprintf(command_buf, "%s %s\n", NM_DELETE, filename);
    if (send(ss_fd, command_buf, strlen(command_buf), 0) < 0) {
        perror("Failed to send NM_DELETE to SS");
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        return; // No lock to unlock
    }

    // 11. Wait for ACK from SS
    char ss_resp[MAX_MSG_LEN];
    memset(ss_resp, 0, MAX_MSG_LEN);
    if (read(ss_fd, ss_resp, MAX_MSG_LEN - 1) <= 0) {
        printf("SS failed to respond to DELETE\n");
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        return; // No lock to unlock
    }

    // --- 3. Commit changes (Re-acquire lock) ---

    // 12. If SS says OK, delete from our map
    if (strncmp(ss_resp, RESP_OK, strlen(RESP_OK)) == 0) {
        
        // *** RE-ACQUIRE THE LOCK ***
        pthread_mutex_lock(&file_map_mutex);

        printf("SS confirmed deletion. Removing from map.\n");

        // We must re-find the file index, in case it changed
        // while we were talking to the SS.
        int current_file_index = trie_search(filename);
        if (current_file_index == -1) {
            // This is weird, but could happen.
            printf("File was already deleted by another thread.\n");
            pthread_mutex_unlock(&file_map_mutex);
            snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_OK); // Just say OK
            send(client_fd, resp_buf, strlen(resp_buf), 0);
            return;
        }

        // Get info about the *last* file before we overwrite
        int last_index = g_file_count - 1;
        char last_filename[MAX_PATH_LEN];
        strcpy(last_filename, g_file_map[last_index].path);

        // 1. Invalidate deleted file from Trie and Cache
        trie_remove(filename);
        cache_remove(filename);

        // 2. Perform the "swap-with-last"
        if (current_file_index != last_index) {
            g_file_map[current_file_index] = g_file_map[last_index];
            
            // 3. Update the *moved* file's index in the Trie
            trie_update_index(last_filename, current_file_index);
        
            // 4. Invalidate the *moved* file's stale entry from Cache
            cache_remove(last_filename);
        }
        g_file_count--;

        save_metadata_to_disk();
        
        // 5. Unlock
        pthread_mutex_unlock(&file_map_mutex);

        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_OK);
        send(client_fd, resp_buf, strlen(resp_buf), 0);

    } else {
        // SS reported an error
        printf("SS reported an error during deletion.\n");
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
    }
}
void do_view(int client_fd, char* requester_username, char* flags) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: VIEW, FLAGS: %s", requester_username, flags);
    log_event(log_msg);
    printf("Client %s requesting VIEW with flags: %s\n", requester_username, flags);

    // Parse the flags
    bool flag_a = (strstr(flags, "a") != NULL);
    bool flag_l = (strstr(flags, "l") != NULL);

    char payload[4096] = ""; // Big buffer
    snprintf(payload, sizeof(payload), "%s\n", RESP_OK);

    pthread_mutex_lock(&file_map_mutex);
    for (int i = 0; i < g_file_count; i++) {
        FileMapEntry* file = &g_file_map[i];
        bool has_access = false;

        // 1. Check for '-a' flag (admin view)
        if (flag_a) {
            has_access = true;
        } else {
            // 2. Run the same ACL check as do_read
            if (strcmp(requester_username, file->owner) == 0) {
                has_access = true;
            }
            if (!has_access) {
                for (int j = 0; j < file->acl_count; j++) {
                    if (strcmp(requester_username, file->acl_list[j].username) == 0) {
                        has_access = true;
                        break;
                    }
                }
            }
        }

        // 3. If they have access, add this file to the payload
        if (has_access) {
            char line_buf[512];
            if (flag_l) {
                // Stub metadata as 0,0 for now, as planned
                char time_str[100];
                struct tm ltime;
                localtime_r(&file->accessed_at, &ltime);
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &ltime);
                
                snprintf(line_buf, sizeof(line_buf), "| %-10s | %5d | %5d | %-16s | %-5s |\n", 
                         file->path, file->word_count, file->char_count, time_str, file->owner);
            } else {
                snprintf(line_buf, sizeof(line_buf), "%s\n", file->path);
            }
            // Safely append to the payload
            strncat(payload, line_buf, sizeof(payload) - strlen(payload) - 1);
        }
    }
    pthread_mutex_unlock(&file_map_mutex);

    send(client_fd, payload, strlen(payload), 0);
}

int registry_has_user(const char* name) {
    for (int i = 0; i < g_user_registry_count; i++) {
        if (strcmp(g_user_registry[i].username, name) == 0)
            return 1;
    }
    return 0;
}

void registry_add_user(const char* name) {
    // Don't add duplicates
    if (registry_has_user(name))
        return;

    strcpy(g_user_registry[g_user_registry_count].username, name);
    g_user_registry_count++;
}


void do_list_users(int client_fd) {
    char username[MAX_USERNAME_LEN] = "unknown";
    pthread_mutex_lock(&client_list_mutex);
    for(int i=0; i<g_client_count; i++) {
        if(g_client_list[i].conn_fd == client_fd) {
            strcpy(username, g_client_list[i].username);
            break;
        }
    }
    pthread_mutex_unlock(&client_list_mutex);
    
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: LIST", username);
    log_event(log_msg);
    printf("Client %s requested user list.\n", username);

    // We'll build a big string payload. 
    // 4096 is a safe, large size.
    char payload[4096] = ""; 

    // Start the response with 200 OK
    snprintf(payload, sizeof(payload), "%s\n", RESP_OK);

    pthread_mutex_lock(&registry_mutex);
for (int i = 0; i < g_user_registry_count; i++) {
    snprintf(payload + strlen(payload), sizeof(payload) - strlen(payload),
             "%s\n", g_user_registry[i].username);
}
pthread_mutex_unlock(&registry_mutex);


    // Send the whole list in one go
    send(client_fd, payload, strlen(payload), 0);
}

void do_info(int client_fd, char* requester_username, char* filename) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: INFO, FILE: %s", requester_username, filename);
    log_event(log_msg);
    printf("Client %s requesting INFO for: %s\n", requester_username, filename);

    char payload[4096] = ""; // Big buffer
    char resp_buf[MAX_MSG_LEN];

    // 1. Lock the central mutex
    pthread_mutex_lock(&file_map_mutex);

    int file_index = -1;
    // char resp_buf[MAX_MSG_LEN]; // Make sure resp_buf is declared

    // 2. Check cache (fastest)
    CacheEntry* entry = cache_find(filename);
    
    if (entry) {
        // --- CACHE HIT ---
        file_index = entry->file_index;
        cache_move_to_front(entry); // Mark as recently used
    } else {
        // --- CACHE MISS ---
        // 3. Check trie (fast)
        file_index = trie_search(filename);
        if (file_index != -1) {
            // 4. Add to cache if we found it
            cache_add(filename, file_index);
        }
    }
    
    // 5. Handle "Not Found"
    if (file_index == -1) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_NOT_FOUND);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return; // Exit the function
    }
    
    // --- We now have a valid file_index, and the lock is still held ---
    FileMapEntry* file = &g_file_map[file_index];
    
    bool has_access = false;

    if (strcmp(requester_username, file->owner) == 0) {
        has_access = true;
    }
    if (!has_access) {
        for (int j = 0; j < file->acl_count; j++) {
            if (strcmp(requester_username, file->acl_list[j].username) == 0) {
                has_access = true;
                break;
            }
        }
    }
    if (!has_access) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_FORBIDDEN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }

    // 3. If access OK, build the detailed payload
    snprintf(payload, sizeof(payload), "%s\n", RESP_OK); // Start with 200 OK
    sprintf(payload + strlen(payload), "File: %s\n", file->path);
    sprintf(payload + strlen(payload), "Owner: %s\n", file->owner);
    sprintf(payload + strlen(payload), "Size: %d words, %d chars\n", file->word_count, file->char_count);

    char time_str[100];
    struct tm ltime;

    // Format Created time
    localtime_r(&file->created_at, &ltime);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &ltime);
    sprintf(payload + strlen(payload), "Created: %s\n", time_str);

    // Format Modified time
    localtime_r(&file->modified_at, &ltime);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &ltime);
    sprintf(payload + strlen(payload), "Last Modified: %s\n", time_str);

    // Format Accessed time
    localtime_r(&file->accessed_at, &ltime);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &ltime);
    sprintf(payload + strlen(payload), "Last Accessed: %s\n", time_str);

    sprintf(payload + strlen(payload), "---Access List---\n");
    sprintf(payload + strlen(payload), "  %s (Owner)\n", file->owner);

    for (int j = 0; j < file->acl_count; j++) {
        sprintf(payload + strlen(payload), "  %s (%c)\n", file->acl_list[j].username, file->acl_list[j].permission);
    }

    pthread_mutex_unlock(&file_map_mutex);
    send(client_fd, payload, strlen(payload), 0);
}

/*
 * ============================================================================
 * CHECKPOINT HANDLER FUNCTIONS
 * ============================================================================
 */

void do_checkpoint(int client_fd, char* username, char* filename, char* tag) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: CHECKPOINT, FILE: %s, TAG: %s", 
             username, filename, tag);
    log_event(log_msg);
    printf("Client %s requesting CHECKPOINT for: %s (tag: %s)\n", username, filename, tag);

    char resp_buf[MAX_MSG_LEN];
    
    // 1. Find file and check ownership
    pthread_mutex_lock(&file_map_mutex);
    
    int file_index = trie_search(filename);
    if (file_index == -1) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_NOT_FOUND);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    FileMapEntry* file = &g_file_map[file_index];
    
    // --- SAFETY CHECK ---
    if (file->is_directory) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s Cannot perform file operation on a folder\n", RESP_BAD_REQ);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    // --------------------
    
    // Only owner can create checkpoints
    if (strcmp(username, file->owner) != 0) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_FORBIDDEN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    int ss_index = file->ss_index;
    pthread_mutex_unlock(&file_map_mutex);
    
    // 2. Forward command to SS
    pthread_mutex_lock(&ss_list_mutex_ext);
    int ss_fd = g_ss_list_ext[ss_index].conn_fd;
    pthread_mutex_unlock(&ss_list_mutex_ext);
    
    char command_buf[MAX_MSG_LEN];
    snprintf(command_buf, MAX_MSG_LEN, "%s %s %s\n", NM_CHECKPOINT, filename, tag);
    
    if (send(ss_fd, command_buf, strlen(command_buf), 0) < 0) {
        perror("Failed to send NM_CHECKPOINT to SS");
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        return;
    }
    
    // 3. Wait for SS response
    char ss_resp[MAX_MSG_LEN];
    memset(ss_resp, 0, MAX_MSG_LEN);
    if (read(ss_fd, ss_resp, MAX_MSG_LEN - 1) <= 0) {
        printf("SS failed to respond to CHECKPOINT\n");
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        return;
    }
    
    // 4. Relay response to client
    send(client_fd, ss_resp, strlen(ss_resp), 0);
}

void do_viewcheckpoint(int client_fd, char* username, char* filename, char* tag) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: VIEWCHECKPOINT, FILE: %s, TAG: %s", 
             username, filename, tag);
    log_event(log_msg);

    char resp_buf[MAX_MSG_LEN];
    
    // --- 1. ACL Check (Same as do_read) ---
    pthread_mutex_lock(&file_map_mutex);
    
    int file_index = trie_search(filename);
    if (file_index == -1) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_NOT_FOUND);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    FileMapEntry* file = &g_file_map[file_index];
    bool has_access = false;
    if (strcmp(username, file->owner) == 0) has_access = true;
    if (!has_access) {
        for (int i = 0; i < file->acl_count; i++) {
            if (strcmp(username, file->acl_list[i].username) == 0) {
                has_access = true;
                break;
            }
        }
    }
    
    if (!has_access) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_FORBIDDEN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    int ss_index = file->ss_index;
    pthread_mutex_unlock(&file_map_mutex);
    
    // --- 2. Get SS Client-Facing Info (like do_exec) ---
    pthread_mutex_lock(&ss_list_mutex_ext);
    char ss_ip[INET_ADDRSTRLEN];
    int ss_port = g_ss_list_ext[ss_index].client_port;
    strcpy(ss_ip, g_ss_list_ext[ss_index].ip);        
    pthread_mutex_unlock(&ss_list_mutex_ext);

    // --- 3. NM connects TO SS on a NEW socket ---
    int ss_sock = connect_to_server(ss_ip, ss_port);
    if (ss_sock < 0) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SS_DOWN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        return;
    }

    // --- 4. Request Checkpoint from SS (using new command) ---
    char req_buf[MAX_MSG_LEN];
    snprintf(req_buf, MAX_MSG_LEN, "%s %s %s\n", SS_VIEWCHECKPOINT, filename, tag);
    send(ss_sock, req_buf, strlen(req_buf), 0);

    // --- 5. Read SS response (status code first) ---
    char ss_resp[MAX_MSG_LEN];
    int n = read(ss_sock, ss_resp, sizeof(ss_resp) - 1);
    if (n <= 0) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        close(ss_sock);
        return;
    }
    ss_resp[n] = '\0';

    // --- 6. Check SS response and relay to client ---
    if (strncmp(ss_resp, RESP_OK, 3) != 0) {
        // It's an error (e.g., 404), just forward it
        send(client_fd, ss_resp, strlen(ss_resp), 0);
    } else {
        // It's 200 OK. Send 200 to our client
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_OK);
        send(client_fd, resp_buf, strlen(resp_buf), 0);

        // Check if data was already in the first read
        char* payload = strchr(ss_resp, '\n');
        if (payload && strlen(payload + 1) > 0) {
            send(client_fd, payload + 1, strlen(payload + 1), 0);
        }

        // Loop to read and relay the rest of the content
        char content_buffer[4096];
        while ((n = read(ss_sock, content_buffer, sizeof(content_buffer))) > 0) {
            send(client_fd, content_buffer, n, 0);
        }

        // Send the "done" marker to our client
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_EXEC_DONE);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
    }

    close(ss_sock); // Close the temporary socket
}

void do_revert(int client_fd, char* username, char* filename, char* tag) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: REVERT, FILE: %s, TAG: %s", 
             username, filename, tag);
    log_event(log_msg);

    char resp_buf[MAX_MSG_LEN];
    
    pthread_mutex_lock(&file_map_mutex);
    
    int file_index = trie_search(filename);
    if (file_index == -1) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_NOT_FOUND);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    FileMapEntry* file = &g_file_map[file_index];
    
    // --- SAFETY CHECK ---
    if (file->is_directory) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s Cannot perform file operation on a folder\n", RESP_BAD_REQ);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    // --------------------
    
    // Check write access (same as WRITE)
    bool has_write_access = false;
    if (strcmp(username, file->owner) == 0) {
        has_write_access = true;
    }
    if (!has_write_access) {
        for (int i = 0; i < file->acl_count; i++) {
            if (strcmp(username, file->acl_list[i].username) == 0) {
                if (file->acl_list[i].permission == PERM_WRITE) {
                    has_write_access = true;
                }
                break;
            }
        }
    }
    
    if (!has_write_access) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_FORBIDDEN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    int ss_index = file->ss_index;
    pthread_mutex_unlock(&file_map_mutex);
    
    // Forward to SS
    pthread_mutex_lock(&ss_list_mutex_ext);
    int ss_fd = g_ss_list_ext[ss_index].conn_fd;
    pthread_mutex_unlock(&ss_list_mutex_ext);
    
    char command_buf[MAX_MSG_LEN];
    snprintf(command_buf, MAX_MSG_LEN, "%s %s %s\n", NM_REVERT, filename, tag);
    send(ss_fd, command_buf, strlen(command_buf), 0);
    
    char ss_resp[MAX_MSG_LEN];
    memset(ss_resp, 0, MAX_MSG_LEN);
    if (read(ss_fd, ss_resp, MAX_MSG_LEN - 1) <= 0) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        return;
    }
    
    // Update metadata on success
    if (strncmp(ss_resp, RESP_OK, 3) == 0) {
        pthread_mutex_lock(&file_map_mutex);
        int current_index = trie_search(filename);
        if (current_index != -1) {
            g_file_map[current_index].modified_at = time(NULL);
            save_metadata_to_disk();
        }
        pthread_mutex_unlock(&file_map_mutex);
    }
    
    send(client_fd, ss_resp, strlen(ss_resp), 0);
}

void do_listcheckpoints(int client_fd, char* username, char* filename) {
    char log_msg[MAX_MSG_LEN];
    snprintf(log_msg, MAX_MSG_LEN, "USER: %s, REQ: LISTCHECKPOINTS, FILE: %s", 
             username, filename);
    log_event(log_msg);

    char resp_buf[MAX_MSG_LEN];
    
    // --- 1. ACL Check (Same as do_read) ---
    pthread_mutex_lock(&file_map_mutex);
    
    int file_index = trie_search(filename);
    if (file_index == -1) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_NOT_FOUND);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    FileMapEntry* file = &g_file_map[file_index];
    bool has_access = false;
    if (strcmp(username, file->owner) == 0) has_access = true;
    if (!has_access) {
        for (int i = 0; i < file->acl_count; i++) {
            if (strcmp(username, file->acl_list[i].username) == 0) {
                has_access = true;
                break;
            }
        }
    }
    
    if (!has_access) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_FORBIDDEN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        pthread_mutex_unlock(&file_map_mutex);
        return;
    }
    
    int ss_index = file->ss_index;
    pthread_mutex_unlock(&file_map_mutex);
    
    // 4. Get SS client-facing IP/port
   // 4. Get SS client-facing IP/port
    pthread_mutex_lock(&ss_list_mutex_ext);      
    char ss_ip[INET_ADDRSTRLEN];
    int ss_port = g_ss_list_ext[ss_index].client_port;
    strcpy(ss_ip, g_ss_list_ext[ss_index].ip); 
    pthread_mutex_unlock(&ss_list_mutex_ext);    
    
    // --- 3. NM connects TO SS on a NEW socket ---
    int ss_sock = connect_to_server(ss_ip, ss_port);
    if (ss_sock < 0) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SS_DOWN);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        return;
    }

    // --- 4. Request List from SS (using new command) ---
    char req_buf[MAX_MSG_LEN];
    snprintf(req_buf, MAX_MSG_LEN, "%s %s\n", SS_LISTCHECKPOINTS, filename);
    send(ss_sock, req_buf, strlen(req_buf), 0);

    // --- 5. Read SS response (status code first) ---
    char ss_resp[MAX_MSG_LEN];
    int n = read(ss_sock, ss_resp, sizeof(ss_resp) - 1);
    if (n <= 0) {
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
        close(ss_sock);
        return;
    }
    ss_resp[n] = '\0';

    // --- 6. Check SS response and relay to client ---
    if (strncmp(ss_resp, RESP_OK, 3) != 0) {
        // It's an error (e.g., 500), just forward it
        send(client_fd, ss_resp, strlen(ss_resp), 0);
    } else {
        // It's 200 OK. Send 200 to our client
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_OK);
        send(client_fd, resp_buf, strlen(resp_buf), 0);

        // Check if data was already in the first read
        char* payload = strchr(ss_resp, '\n');
        if (payload && strlen(payload + 1) > 0) {
            send(client_fd, payload + 1, strlen(payload + 1), 0);
        }

        // Loop to read and relay the rest of the list
        char content_buffer[4096];
        while ((n = read(ss_sock, content_buffer, sizeof(content_buffer))) > 0) {
            send(client_fd, content_buffer, n, 0);
        }

        // Send the "done" marker to our client
        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_EXEC_DONE);
        send(client_fd, resp_buf, strlen(resp_buf), 0);
    }

    close(ss_sock); // Close the temporary socket
}

void* handle_client_commands(void* arg) {
    int client_fd = *((int*)arg); // Get the FD
    char buffer[MAX_MSG_LEN];
    char cmd[MAX_MSG_LEN];
    char arg1[MAX_PATH_LEN]; // Use MAX_PATH_LEN for filenames
    char arg2[MAX_PATH_LEN]; // For target_user
    char arg3[MAX_PATH_LEN];              // For flags ("-R" or "-W")
    char resp_buf[MAX_MSG_LEN];

    char username[MAX_USERNAME_LEN] = "unknown";
    pthread_mutex_lock(&client_list_mutex);
    for(int i=0; i<g_client_count; i++) {
        if(g_client_list[i].conn_fd == client_fd) {
            strcpy(username, g_client_list[i].username);
            break;
        }
    }
    pthread_mutex_unlock(&client_list_mutex);


    while (1) {
        memset(buffer, 0, MAX_MSG_LEN);
        memset(cmd, 0, MAX_MSG_LEN);     
        memset(arg1, 0, MAX_PATH_LEN); 
        memset(arg2, 0, MAX_PATH_LEN);
        memset(arg3, 0, MAX_PATH_LEN);

        int bytes_read = read(client_fd, buffer, MAX_MSG_LEN - 1);

        if (bytes_read <= 0) {
            printf("Client %s disconnected.\n", username);
            pthread_mutex_lock(&client_list_mutex);
            int client_index = -1;
            for (int i = 0; i < g_client_count; i++) {
                if (g_client_list[i].conn_fd == client_fd) {
                    client_index = i;
                    break;
                }
            }
            
            if (client_index != -1) {
                // "Swap-with-last" delete
                g_client_list[client_index] = g_client_list[g_client_count - 1];
                g_client_count--;
            }
            pthread_mutex_unlock(&client_list_mutex);            
            break; // Exit loop
        }
        
        int items_scanned = sscanf(buffer, "%1023s %255s %255s %255s", cmd, arg1, arg2, arg3);
        if (items_scanned <= 0) {
            continue; // Ignore empty lines (like just hitting Enter)
        }
        
        // IMPORTANT: Check longer commands FIRST to avoid prefix matching issues
        // (e.g., C_CREATEFOLDER must be checked before C_CREATE)
        if (strncmp(cmd, C_REQ_CREATEFOLDER, strlen(C_REQ_CREATEFOLDER)) == 0) {
            if (items_scanned < 2) {
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
                send(client_fd, resp_buf, strlen(resp_buf), 0);
            } else {
                do_create_folder(client_fd, username, arg1);
            }
        }
        else if (strncmp(cmd, C_REQ_CREATE, strlen(C_REQ_CREATE)) == 0) {
            do_create(client_fd, username, arg1);
        } 
        else if (strncmp(cmd, C_REQ_ADD_ACC, strlen(C_REQ_ADD_ACC)) == 0) {
            if (items_scanned < 4) {
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
                send(client_fd, resp_buf, strlen(resp_buf), 0);
            } else {
                // Robustness check: ensure arg1 is actually the flag
                if (arg1[0] == '-') {
                    do_add_access(client_fd, username, arg2, arg3, arg1[1]);
                } else {
                    // Handle legacy case or user error (File User Flag)
                    // This makes it compatible with old clients or netcat mistakes
                    do_add_access(client_fd, username, arg1, arg2, arg3[1]);
                }
            }
        }
        else if (strncmp(cmd, C_REQ_REM_ACC, strlen(C_REQ_REM_ACC)) == 0) {
            if (items_scanned < 3) { // Needs 3 args: CMD, file, user
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
                send(client_fd, resp_buf, strlen(resp_buf), 0);
            } else {
                do_rem_access(client_fd, username, arg1, arg2); // arg1=file, arg2=user
            }
        }
        else if (strcmp(cmd, C_REQ_VIEWFOLDER) == 0) {
            if (items_scanned < 2) {
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
                send(client_fd, resp_buf, strlen(resp_buf), 0);
            } else {
                do_view_folder(client_fd, username, arg1);
            }
        }
        else if (strcmp(cmd, C_REQ_VIEWCHECKPOINT) == 0) {
            if (items_scanned < 3) {
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
                send(client_fd, resp_buf, strlen(resp_buf), 0);
            } else {
                do_viewcheckpoint(client_fd, username, arg1, arg2);
            }
        }
        else if (strcmp(cmd, C_REQ_VIEW_REQUESTS) == 0) {
            if (items_scanned < 2) {
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
                send(client_fd, resp_buf, strlen(resp_buf), 0);
            } else {
                do_view_requests(client_fd, username, arg1);
            }
        }
        else if (strncmp(cmd, C_REQ_READ, strlen(C_REQ_READ)) == 0) {
            do_read(client_fd, username, arg1);
        } 
        else if (strncmp(cmd, C_REQ_WRITE, strlen(C_REQ_WRITE)) == 0) {
            if (items_scanned < 2) { // Needs CMD and filename
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
                send(client_fd, resp_buf, strlen(resp_buf), 0);
            } else {
                do_write(client_fd, username, arg1); // arg1 is filename
            }
        }
        else if (strncmp(cmd, C_REQ_UNDO, strlen(C_REQ_UNDO)) == 0) {
            if (items_scanned < 2) { // Needs CMD and filename
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
                send(client_fd, resp_buf, strlen(resp_buf), 0);
            } else {
                do_undo(client_fd, username, arg1); // arg1 is filename
            }
        }
        else if (strncmp(cmd, C_REQ_STREAM, strlen(C_REQ_STREAM)) == 0) {
            if (items_scanned < 2) { // Needs CMD and filename
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
                send(client_fd, resp_buf, strlen(resp_buf), 0);
            } else {
                // C_STREAM uses the exact same logic as C_READ
                do_read(client_fd, username, arg1); 
            }
        }
        else if (strncmp(cmd, C_REQ_DELETE, strlen(C_REQ_DELETE)) == 0) {
            if (items_scanned < 2) { // Needs CMD and filename
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
                send(client_fd, resp_buf, strlen(resp_buf), 0);
            } else {
                do_delete(client_fd, username, arg1); // arg1 is filename
            }
        }
        else if (strncmp(cmd, C_REQ_LISTCHECKPOINTS, strlen(C_REQ_LISTCHECKPOINTS)) == 0) {
            if (items_scanned < 2) {
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
                send(client_fd, resp_buf, strlen(resp_buf), 0);
            } else {
                do_listcheckpoints(client_fd, username, arg1);
            }
        }
        else if (strncmp(cmd, C_REQ_LIST, strlen(C_REQ_LIST)) == 0) {
            do_list_users(client_fd);
        }
        else if (strcmp(cmd, C_REQ_VIEW) == 0) {
            do_view(client_fd, username, arg1); // arg1 holds the flags
        }
        else if (strcmp(cmd, C_REQ_INFO) == 0) {
            do_info(client_fd, username, arg1); // arg1 is filename
        }
        else if (strcmp(cmd, C_REQ_EXEC) == 0) {
            if (items_scanned < 2) { // Needs CMD and filename
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
                send(client_fd, resp_buf, strlen(resp_buf), 0);
            } else {
                do_exec(client_fd, username, arg1);
            }
        }
        else if (strcmp(cmd, C_REQ_CHECKPOINT) == 0) {
            if (items_scanned < 3) {
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
                send(client_fd, resp_buf, strlen(resp_buf), 0);
            } else {
                do_checkpoint(client_fd, username, arg1, arg2);
            }
        }

        else if (strncmp(cmd, C_REQ_REVERT, strlen(C_REQ_REVERT)) == 0) {
            if (items_scanned < 3) {
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
                send(client_fd, resp_buf, strlen(resp_buf), 0);
            } else {
                do_revert(client_fd, username, arg1, arg2);
            }
        }
        else if (strncmp(cmd, C_REQ_REQUEST_ACC, strlen(C_REQ_REQUEST_ACC)) == 0) {
            if (items_scanned < 3) {
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
                send(client_fd, resp_buf, strlen(resp_buf), 0);
            } else {
                do_request_access(client_fd, username, arg1, arg2[1]);
            }
        }
        else if (strncmp(cmd, C_REQ_APPROVE_ACC, strlen(C_REQ_APPROVE_ACC)) == 0) {
            if (items_scanned < 2) {
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
                send(client_fd, resp_buf, strlen(resp_buf), 0);
            } else {
                int req_id = atoi(arg1);
                do_approve_request(client_fd, username, req_id);
            }
        }
        else if (strncmp(cmd, C_REQ_DENY_ACC, strlen(C_REQ_DENY_ACC)) == 0) {
            if (items_scanned < 2) {
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
                send(client_fd, resp_buf, strlen(resp_buf), 0);
            } else {
                int req_id = atoi(arg1);
                do_deny_request(client_fd, username, req_id);
            }
        }
        else if (strncmp(cmd, C_REQ_MY_REQUESTS, strlen(C_REQ_MY_REQUESTS)) == 0) {
            do_my_requests(client_fd, username);
        }
        else if (strncmp(cmd, C_REQ_MOVE, strlen(C_REQ_MOVE)) == 0) {
            if (items_scanned < 3) {
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
                send(client_fd, resp_buf, strlen(resp_buf), 0);
            } else {
                do_move(client_fd, username, arg1, arg2);
            }
        }
        else {
            // Now this is a *real* unknown command
            snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
            send(client_fd, resp_buf, strlen(resp_buf), 0);
        }
    }
    
    close(client_fd);
    free(arg); 
    return NULL;
}

int main(int argc, char*argv[]){
    printf("Starting server...\n");
    load_metadata_from_disk();
    
    g_file_trie_root = create_trie_node();
    build_trie_from_map();

    // Initialize replication system
    init_replication_system();

    g_log_fp = fopen(NM_LOG_FILE, "a"); // "a" for append
    if (g_log_fp == NULL) {
        perror("FATAL: Failed to open log file");
        exit(1);
    }
    // Set the "close-on-exec" flag for the log file
    int log_fd = fileno(g_log_fp);
    fcntl(log_fd, F_SETFD, FD_CLOEXEC);

    log_event("--- Name Server Started ---");

    int server_fd; //server file descriptor
    server_fd=socket(AF_INET,SOCK_STREAM,0);
    // AF_INET = Use IPv4 (the standard internet protocol)
    // SOCK_STREAM = Use TCP (a reliable, streaming connection, not UDP)
    // 0 = Use the default protocol (which is TCP for SOCK_STREAM)
    
    // ALWAYS check for errors.
    // A negative return value means the function failed.
    if(server_fd<0){
        // perror prints your message ("socket() failed")
        // AND the specific system error (like "Permission denied").
        perror("socket() function failed");
        exit(1);
    }
    printf("1. Socket created successfully (fd=%d) \n",server_fd);

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        exit(1);
    }
    //When your server exits (or crashes), the operating system keeps port 9000 in a "TIME_WAIT" state for about 30-60 seconds. It's "reserving" the port just in case any last-second data packets arrive.
    //The Solution: SO_REUSEADDR, We need to tell the OS, "Hey, I'm the new server, and I give you permission to reuse that address right now.", We do this with a function called setsockopt().

    struct sockaddr_in server_addr;
    //Different kinds of networks (IPv4, IPv6, local domain sockets, etc.) each have their own struct to represent an address.
    //AF_INET and struct sockaddr_in for IPv4, AF_INET6 and struct sockaddr_in6 for IPv6, etc.


    int port=NM_LISTEN_PORT;
    //we can also take this from command-line using if (argc > 1) port = atoi(argv[1]);

    // Clear the entire struct to make sure it's clean
    // We want all fields clean before assigning values.
    // Prevents accidental garbage values from stack memory.
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    // Sets the address family to IPv4 (must match what we used in the socket else bind() will fail with EINVAL (invalid argument))

    server_addr.sin_addr.s_addr = INADDR_ANY;
    // Sets the IP address to INADDR_ANY which means "listen on any available IP address" This is what we want for a server.
    //INADDR_ANY is a macro meaning “all network interfaces”.In binary, it’s 0.0.0.0. 

    server_addr.sin_port = htons(port);
    // Sets the port number, converting from host byte order to network byte order
    // htons() = "Host to Network Short"
    // All TCP/IP headers use network byte order (big-endian).
    // If you skip this conversion, other computers would interpret the bytes backwards — e.g.,
    // 9000 (0x2328) might become 0x2823 = 10275, i.e., completely wrong port.

    //bind() is the system call that tells the operating system, "I want the socket server_fd to be the one responsible for port 9000 (we have assigned server_addr's sin_port as port)."
    if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        // We cast our 'struct sockaddr_in' (which is internet-specific)
        // to the generic 'struct sockaddr' that bind() expects.
        perror("bind() failed");
        //checking for errors. A common error here is "Address already in use," which means another program (or your old, crashed server) is still holding onto port 9000.
        exit(1);
    }
    printf("2. Socket bound to port %d\n", port);

    // listen(server_fd, 5) tells the OS: "If I'm busy in my accept() loop handling a new connection, you can hold up to 5 other new connections in a 'pending' queue. If a 6th connection arrives while the queue is full, just reject it."
    // Since your accept() loop is so fast (it just calls pthread_create and loops), the backlog is rarely hit, but it's important to have. 5 or 20 is a fine number for this.
    if (listen(server_fd, 5) < 0) {
        perror("listen() failed");
        exit(1);
    }
    
    printf("3. Server is listening on port %d...\n", port);

    // Start heartbeat monitoring thread
    pthread_t heartbeat_tid;
    if (pthread_create(&heartbeat_tid, NULL, heartbeat_thread, NULL) != 0) {
        perror("Failed to create heartbeat thread");
        exit(1);
    }
    pthread_detach(heartbeat_tid);
    printf("[HEARTBEAT] Heartbeat thread started\n");
    
    // Start async write thread for replication
    pthread_t async_write_tid;
    if (pthread_create(&async_write_tid, NULL, async_write_thread, NULL) != 0) {
        perror("Failed to create async write thread");
        exit(1);
    }
    pthread_detach(async_write_tid);
    printf("[REPLICATION] Async write thread started\n");

    int client_fd; // This will be the NEW file descriptor for the client
    struct sockaddr_in client_addr; // This will hold the CLIENT's address info
    socklen_t client_len = sizeof(client_addr);
    printf("Waiting for a client to connect...\n");
    while (1) {
        // now accept() blocks the program and waits for a connection
        client_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_len);
        
        if (client_fd < 0) {
            perror("accept() failed");
            continue; // Go back to the start of the loop and wait again
        }

        // printf("4. Client connected successfully! Waiting for a message...\n");
        printf("4. Client connected! Handing off to a new thread...\n");

        pthread_t tid;

        int *new_sock=malloc(sizeof(int));
        *new_sock=client_fd;
        // We can't just pass &client_fd to the thread. Because the main loop will immediately loop back, accept a new client, and change the value of client_fd. The first thread would suddenly have its file descriptor changed! By malloc-ing new memory, we give each thread its own private copy of the file descriptor.
    // Why not pass &client_fd? If you passed the address of the stack variable client_fd from the main thread, that memory could change when the main loop accepts the next connection; threads would race and get wrong FDs. Allocating per-thread memory avoids that race.

        if(pthread_create(&tid, NULL, handle_connection, (void*)new_sock)!=0){
            perror("pthread_create() failed");
        }
        // Create a new thread:
        // 1. &tid: Store the thread's ID here
        // 2. NULL: Use default thread attributes
        // 3. handle_client: The function the new thread should run
        // 4. new_sock: The argument to pass to that function
        // We need to pass the client_fd to the thread, but pthread_create only accepts a (void*) so we cast.

    }
    close(server_fd);
    return 0;
}

// ============================================================================
// REPLICATION & FAULT TOLERANCE IMPLEMENTATION
// ============================================================================

void init_replication_system() {
    pthread_mutex_lock(&ss_list_mutex_ext);
    g_ss_count_ext = 0;
    pthread_mutex_unlock(&ss_list_mutex_ext);
    
    pthread_mutex_lock(&replica_mutex);
    g_file_replica_count = 0;
    pthread_mutex_unlock(&replica_mutex);
    
    pthread_mutex_lock(&async_write_mutex);
    g_async_write_count = 0;
    pthread_mutex_unlock(&async_write_mutex);
    
    printf("[REPLICATION] Replication system initialized (factor=%d)\n", REPLICATION_FACTOR);
}

int select_replica_ss(int exclude_ss_index) {
    // Select a storage server for replication, excluding the specified index
    // Returns -1 if no suitable server found
    
    pthread_mutex_lock(&ss_list_mutex_ext);
    
    if (g_ss_count_ext <= 1) {
        pthread_mutex_unlock(&ss_list_mutex_ext);
        return -1; // No other servers available
    }
    
    // Use static counter to distribute replicas across different servers
    static int last_selected = 0;
    int attempts = 0;
    int candidate = (last_selected + 1) % g_ss_count_ext;
    
    while (attempts < g_ss_count_ext) {
        if (candidate != exclude_ss_index) {
            last_selected = candidate;
            pthread_mutex_unlock(&ss_list_mutex_ext);
            return candidate;
        }
        candidate = (candidate + 1) % g_ss_count_ext;
        attempts++;
    }
    
    pthread_mutex_unlock(&ss_list_mutex_ext);
    return -1;
}

void enqueue_async_write(const char* filename, const char* operation, int target_ss) {
    pthread_mutex_lock(&async_write_mutex);
    
    if (g_async_write_count >= MAX_PENDING_WRITES * MAX_SS) {
        printf("[REPLICATION] WARNING: Async write queue full, dropping task\n");
        pthread_mutex_unlock(&async_write_mutex);
        return;
    }
    
    // **FIX: Check if target SS is actually online before enqueuing**
    pthread_mutex_lock(&ss_list_mutex_ext);
    bool ss_online = (target_ss < g_ss_count_ext && 
                      g_ss_list_ext[target_ss].status == SS_STATUS_ONLINE);
    pthread_mutex_unlock(&ss_list_mutex_ext);
    
    if (!ss_online) {
        printf("[REPLICATION] Skipping enqueue - SS[%d] is offline\n", target_ss);
        pthread_mutex_unlock(&async_write_mutex);
        return;
    }
    
    AsyncWriteTask* task = &g_async_write_queue[g_async_write_count];
    strncpy(task->filename, filename, MAX_PATH_LEN - 1);
    task->filename[MAX_PATH_LEN - 1] = '\0';
    strncpy(task->operation, operation, 31);
    task->operation[31] = '\0';
    task->target_ss_index = target_ss;
    task->queued_at = time(NULL);
    
    g_async_write_count++;
    
    pthread_mutex_unlock(&async_write_mutex);
    
    printf("[REPLICATION] Enqueued %s for '%s' to SS[%d] (queue size: %d)\n",
           operation, filename, target_ss, g_async_write_count);
}

void* heartbeat_thread(void* arg) {
    printf("[HEARTBEAT] Heartbeat monitoring thread started\n");
    
    while (1) {
        sleep(HEARTBEAT_INTERVAL);
        
        pthread_mutex_lock(&ss_list_mutex_ext);
        
        for (int i = 0; i < g_ss_count_ext; i++) {
            if (g_ss_list_ext[i].status != SS_STATUS_ONLINE) {
                continue; // Skip offline or recovering servers
            }
            
            // Send heartbeat ping (fire and forget - no ACK read to avoid socket contention)
            char heartbeat_msg[MAX_MSG_LEN];
            snprintf(heartbeat_msg, MAX_MSG_LEN, "%s\n", NM_HEARTBEAT);
            
            int bytes_sent = send(g_ss_list_ext[i].conn_fd, heartbeat_msg, strlen(heartbeat_msg), MSG_NOSIGNAL);
            
            if (bytes_sent > 0) {
                // Successfully sent - assume SS is alive
                // We'll detect failures through send() errors or command timeouts
                g_ss_list_ext[i].last_heartbeat = time(NULL);
                printf("[HEARTBEAT] Ping sent to SS[%d] - connection alive\n", i);
            } else {
                printf("[HEARTBEAT] Failed to send to SS[%d]: %s\n", 
                       i, strerror(errno));
                       
                // Check for timeout and trigger failure handling
                time_t now = time(NULL);
                if (now - g_ss_list_ext[i].last_heartbeat > SS_TIMEOUT) {
                    printf("[HEARTBEAT] SS[%d] timed out (last seen %ld seconds ago) - triggering failure handling\n",
                           i, now - g_ss_list_ext[i].last_heartbeat);
                    // 1. Release lock to prevent Deadlock
                    pthread_mutex_unlock(&ss_list_mutex_ext);
                    
                    // 2. Call handler
                    handle_ss_failure(i);
                    
                    // 3. Re-acquire lock to continue loop safely
                    pthread_mutex_lock(&ss_list_mutex_ext);
                }
            }
        }
        
        pthread_mutex_unlock(&ss_list_mutex_ext);
    }
    
    return NULL;
}

void* async_write_thread(void* arg) {
    printf("[REPLICATION] Async write thread started\n");
    
    while (1) {
        usleep(100000); // Sleep 100ms between queue checks
        
        pthread_mutex_lock(&async_write_mutex);
        
        if (g_async_write_count == 0) {
            pthread_mutex_unlock(&async_write_mutex);
            continue;
        }
        
        // Process first task in queue
        AsyncWriteTask task = g_async_write_queue[0];
        
        // Remove from queue (shift remaining tasks)
        for (int i = 0; i < g_async_write_count - 1; i++) {
            g_async_write_queue[i] = g_async_write_queue[i + 1];
        }
        g_async_write_count--;
        
        pthread_mutex_unlock(&async_write_mutex);
        
        // **CRITICAL FIX: Use a DEDICATED LOCK for async writes**
        // This prevents interference with do_create's synchronous read
        static pthread_mutex_t async_send_mutex = PTHREAD_MUTEX_INITIALIZER;
        
        pthread_mutex_lock(&async_send_mutex);
        pthread_mutex_lock(&ss_list_mutex_ext);
        
        if (task.target_ss_index < g_ss_count_ext && 
            g_ss_list_ext[task.target_ss_index].status == SS_STATUS_ONLINE) {
            
            int ss_fd = g_ss_list_ext[task.target_ss_index].conn_fd;
            
            // Send command to replica SS
            char cmd_buf[MAX_MSG_LEN];
            snprintf(cmd_buf, MAX_MSG_LEN, "%s %s\n", task.operation, task.filename);
            
            ssize_t sent = send(ss_fd, cmd_buf, strlen(cmd_buf), MSG_NOSIGNAL);
            
            if (sent > 0) {
                printf("[REPLICATION] Sent %s to SS[%d] for '%s'\n", 
                       task.operation, task.target_ss_index, task.filename);
                
                // **FIX: Read the ACK with proper error handling**
                char ack_buf[MAX_MSG_LEN];
                memset(ack_buf, 0, MAX_MSG_LEN);
                
                // Set a 2 second timeout for async ACK
                struct timeval tv = {.tv_sec = 2, .tv_usec = 0};
                setsockopt(ss_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                
                ssize_t n = read(ss_fd, ack_buf, MAX_MSG_LEN - 1);
                
                if (n > 0) {
                    ack_buf[n] = '\0';
                    // Remove any trailing newlines for cleaner logging
                    char *newline = strchr(ack_buf, '\n');
                    if (newline) *newline = '\0';
                    
                    printf("[REPLICATION] ACK from SS[%d]: '%s'\n", 
                           task.target_ss_index, ack_buf);
                } else if (n == 0) {
                    printf("[REPLICATION] SS[%d] closed connection during ACK\n", 
                           task.target_ss_index);
                } else {
                    // n < 0, check if it's timeout or real error
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        printf("[REPLICATION] Timeout waiting for ACK from SS[%d]\n", 
                               task.target_ss_index);
                    } else {
                        printf("[REPLICATION] Error reading ACK from SS[%d]: %s\n", 
                               task.target_ss_index, strerror(errno));
                    }
                }
                
                // **CRITICAL: Reset socket to blocking mode**
                tv.tv_sec = 0;
                tv.tv_usec = 0;
                setsockopt(ss_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                
            } else {
                printf("[REPLICATION] Failed to send to SS[%d]: %s\n", 
                       task.target_ss_index, strerror(errno));
            }
        } else {
            printf("[REPLICATION] SS[%d] is offline or invalid, skipping replication\n", 
                   task.target_ss_index);
        }
        
        pthread_mutex_unlock(&ss_list_mutex_ext);
        pthread_mutex_unlock(&async_send_mutex);
        
        // Small delay between async operations
        usleep(50000); // 50ms
    }
    
    return NULL;
}

void handle_ss_failure(int failed_ss_index) {
    printf("[FAILURE] Handling failure of SS[%d]\n", failed_ss_index);
    
    pthread_mutex_lock(&ss_list_mutex_ext);
    
    time_t now = time(NULL);

    // 1. Check if it is RECOVERING (It genuinely reconnected)
    if (g_ss_list_ext[failed_ss_index].status == SS_STATUS_RECOVERING) {
        printf("[FAILURE] SS[%d] is recovering. Aborting failover.\n", failed_ss_index);
        pthread_mutex_unlock(&ss_list_mutex_ext);
        return;
    }

    // 2. Check if it is ONLINE but has a NEW heartbeat (Race condition save)
    // If the heartbeat is recent (less than timeout), it means it came back to life while we were unlocked.
    if (g_ss_list_ext[failed_ss_index].status == SS_STATUS_ONLINE && 
        (now - g_ss_list_ext[failed_ss_index].last_heartbeat < SS_TIMEOUT)) {
        printf("[FAILURE] SS[%d] reconnected (fresh heartbeat). Aborting failover.\n", failed_ss_index);
        pthread_mutex_unlock(&ss_list_mutex_ext);
        return; 
    }

    // 3. If we are here, it is truly dead. Mark it OFFLINE.
    g_ss_list_ext[failed_ss_index].status = SS_STATUS_OFFLINE;
    
    pthread_mutex_unlock(&ss_list_mutex_ext);
    
    // --- Proceed with Failover Logic (No changes needed below this line) ---

    // Update file map to point to replica servers
    pthread_mutex_lock(&file_map_mutex);
    pthread_mutex_lock(&replica_mutex);
    
    for (int i = 0; i < g_file_replica_count; i++) {
        FileReplicationEntry* rep = &g_file_replicas[i];
        
        // Check if this file's primary server failed
        if (rep->replica_ss_indices[0] == failed_ss_index && rep->replica_count > 1) {
            // Promote first available replica to primary
            int new_primary_idx = rep->replica_ss_indices[1];
            printf("[FAILURE] Promoting SS[%d] to primary for file '%s' (was SS[%d])\n",
                   new_primary_idx, rep->path, failed_ss_index);
            
            // Update file map to point to new primary
            for (int j = 0; j < g_file_count; j++) {
                if (strcmp(g_file_map[j].path, rep->path) == 0) {
                    g_file_map[j].ss_index = new_primary_idx;
                    break;
                }
            }
            
            // Shift replicas down
            rep->replica_ss_indices[0] = rep->replica_ss_indices[1];
            if (rep->replica_count > 2) {
                rep->replica_ss_indices[1] = rep->replica_ss_indices[2];
            }
            rep->replica_count--;
            
            printf("[FAILURE] File '%s' now has %d replicas\n", rep->path, rep->replica_count);
        }
        // Also remove from replica list if it was a replica
        else {
            for (int j = 1; j < rep->replica_count; j++) {
                if (rep->replica_ss_indices[j] == failed_ss_index) {
                    // Shift remaining replicas
                    for (int k = j; k < rep->replica_count - 1; k++) {
                        rep->replica_ss_indices[k] = rep->replica_ss_indices[k + 1];
                    }
                    rep->replica_count--;
                    printf("[FAILURE] Removed failed SS[%d] from replica list for '%s'\n",
                           failed_ss_index, rep->path);
                    break;
                }
            }
        }
    }
    
    pthread_mutex_unlock(&replica_mutex);
    pthread_mutex_unlock(&file_map_mutex);
    
    printf("[FAILURE] Failover complete for SS[%d]\n", failed_ss_index);
}

void* handle_ss_recovery(void* arg) {
    int recovered_ss_index = *((int*)arg);
    free(arg);

    printf("[RECOVERY] Handling recovery of SS[%d]\n", recovered_ss_index);
    
    pthread_mutex_lock(&ss_list_mutex_ext);
    g_ss_list_ext[recovered_ss_index].status = SS_STATUS_RECOVERING;
    pthread_mutex_unlock(&ss_list_mutex_ext);
    
    // LOCK EVERYTHING: We are modifying global replication state
    pthread_mutex_lock(&file_map_mutex);
    pthread_mutex_lock(&replica_mutex);
    
    int synced_count = 0;

    for (int i = 0; i < g_file_replica_count; i++) {
        FileReplicationEntry* rep = &g_file_replicas[i];
        
        bool already_has = false;
        for(int k=0; k<rep->replica_count; k++) {
            if(rep->replica_ss_indices[k] == recovered_ss_index) {
                already_has = true; 
                break;
            }
        }

        if (!already_has && rep->replica_count < REPLICATION_FACTOR) {
            // Find the CURRENT Primary
            int primary_ss_idx = -1;
            for(int f=0; f<g_file_count; f++) {
                if(strcmp(g_file_map[f].path, rep->path) == 0) {
                    primary_ss_idx = g_file_map[f].ss_index;
                    break;
                }
            }

            // Ensure primary is actually online
            pthread_mutex_lock(&ss_list_mutex_ext);
            bool primary_online = (primary_ss_idx != -1 && 
                                   g_ss_list_ext[primary_ss_idx].status == SS_STATUS_ONLINE);
            
            // Retrieve socket and primary info safely while locked
            int ss_fd = -1;
            char primary_ip[INET_ADDRSTRLEN];
            int primary_port = 0;
            
            if (primary_online) {
                ss_fd = g_ss_list_ext[recovered_ss_index].conn_fd;
                strcpy(primary_ip, g_ss_list_ext[primary_ss_idx].ip);
                primary_port = g_ss_list_ext[primary_ss_idx].client_port;
            }
            pthread_mutex_unlock(&ss_list_mutex_ext);

            if (primary_online && ss_fd != -1) {
                char cmd[MAX_MSG_LEN];
                snprintf(cmd, MAX_MSG_LEN, "%s %s %s %d\n", NM_SYNC, rep->path, primary_ip, primary_port);
                
                if (send(ss_fd, cmd, strlen(cmd), MSG_NOSIGNAL) > 0) {
                     char ack[MAX_MSG_LEN];
                     struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
                     setsockopt(ss_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                     
                     if (read(ss_fd, ack, MAX_MSG_LEN) > 0 && strncmp(ack, "200", 3) == 0) {
                         rep->replica_ss_indices[rep->replica_count] = recovered_ss_index;
                         rep->replica_count++;
                         synced_count++;
                         printf("[RECOVERY] Restored replica of '%s' to SS[%d]\n", rep->path, recovered_ss_index);
                     }
                }
            }
        }
    }
    
    pthread_mutex_unlock(&replica_mutex);
    pthread_mutex_unlock(&file_map_mutex);
    
    pthread_mutex_lock(&ss_list_mutex_ext);
    g_ss_list_ext[recovered_ss_index].status = SS_STATUS_ONLINE;
    g_ss_list_ext[recovered_ss_index].last_heartbeat = time(NULL);
    pthread_mutex_unlock(&ss_list_mutex_ext);
    
    printf("[RECOVERY] Recovery complete for SS[%d]: synced %d files\n", recovered_ss_index, synced_count);
    
    return NULL;
}
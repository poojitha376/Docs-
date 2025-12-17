#include "protocol.h" // Your protocol
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>      // For gethostbyname
#include <pthread.h>    // For threads
#include <stdbool.h>
#include <time.h>
#include <sys/stat.h> 
#include <sys/types.h>
#include <errno.h>
#include <dirent.h>   // For directory operations
#include <arpa/inet.h> // For inet_pton    
#include "config.h"  // Add at top
char g_nm_ip[INET_ADDRSTRLEN] = "127.0.0.1";

FILE* g_log_fp = NULL;
pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;
char g_ss_root_path[64];

// #define NM_IP "127.0.0.1" // IP for the Name Server (NM)

// --- Global Lock Manager ---
typedef struct {
    char path[MAX_PATH_LEN];
    bool is_locked;
    int sentence_num;
    // We could also store the user who locked it
} FileLock;

#define MAX_LOCKS 50
FileLock g_lock_list[MAX_LOCKS];
int g_lock_count = 0;
pthread_mutex_t g_lock_list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t g_commit_mutex = PTHREAD_MUTEX_INITIALIZER;
// --- End of Lock Manager ---

// --- Checkpoint Manager ---
typedef struct {
    char tag[MAX_CHECKPOINT_TAG];
    char filename[MAX_PATH_LEN];
    time_t created_at;
    char checkpoint_path[MAX_PATH_LEN]; // Path to the checkpoint file
} Checkpoint;

#define MAX_CHECKPOINTS 200  // Total checkpoints across all files
Checkpoint g_checkpoint_list[MAX_CHECKPOINTS];
int g_checkpoint_count = 0;
pthread_mutex_t g_checkpoint_mutex = PTHREAD_MUTEX_INITIALIZER;
// --- End of Checkpoint Manager ---

// global for our connection to the NM
int g_nm_fd; 

// Struct for passing args to client threads
typedef struct {
    int client_fd;
    int ss_port;
} ClientThreadArgs;

// --- We will create this listener function in the next session ---
void* run_listener_thread(void* arg); 

// Forward declarations for checkpoint functions
char* read_checkpoint(const char* filename, const char* tag);
char* list_checkpoints(const char* filename);

// Helper function to read an entire file into a new, allocated string.
// Returns NULL on failure. Caller MUST free() the result.
char* read_file_to_memory(const char* filepath) {
    FILE *fp = fopen(filepath, "r");
    if (fp == NULL) {
        perror("fopen (read_file_to_memory)");
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *buffer = (char*)malloc(file_size + 1);
    if (buffer == NULL) {
        perror("malloc");
        fclose(fp);
        return NULL;
    }

    fread(buffer, 1, file_size, fp);
    buffer[file_size] = '\0';
    fclose(fp);
    return buffer;
}

// Validates a filename to prevent path traversal attacks.
// Returns true if the filename is safe, false otherwise.
bool is_valid_filename(const char* filename) {
    // 1. Check for absolute paths
    if (filename[0] == '/') {
        printf("[SS-Security] Rejecting absolute path: %s\n", filename);
        return false;
    }
    
    // 2. Check for ".." (directory traversal)
    if (strstr(filename, "..") != NULL) {
        printf("[SS-Security] Rejecting path traversal: %s\n", filename);
        return false;
    }
    
    // 3. Check for empty filename
    if (strlen(filename) == 0) {
        return false;
    }
    
    return true;
}

// Counts words (space-separated) and chars
void count_metadata(char* content, int* word_count, int* char_count) {
    *char_count = strlen(content);
    *word_count = 0;

    char* temp_content = strdup(content); // Make a copy
    char* word = strtok(temp_content, " \n\t");
    while (word != NULL) {
        (*word_count)++;
        word = strtok(NULL, " \n\t");
    }
    free(temp_content);
}

// Helper function to check if string ends with sentence delimiter
bool ends_with_delimiter(const char* str) {
    if (str == NULL || strlen(str) == 0) return false;
    char last = str[strlen(str) - 1];
    return (last == '.' || last == '!' || last == '?');
}

/*
 * Counts the number of sentences in a file's content.
 * An empty file has 0 sentences.
 * A file with content but no delimiter has 1 sentence.
 * Each delimiter (. ! ?) marks the end of a sentence.
 */
int count_sentences(char* content) {
    if (content == NULL || *content == '\0') {
        return 0; // Empty content has 0 sentences
    }

    int sentence_count = 0;
    char* ptr = content;
    
    // Skip leading whitespace
    while (*ptr == ' ' || *ptr == '\n' || *ptr == '\t') {
        ptr++;
    }
    
    if (*ptr == '\0') {
        return 0; // Only whitespace
    }
    
    // We have at least one sentence (even without delimiter)
    sentence_count = 1;
    
    // Count additional sentences by counting delimiters
    while (*ptr != '\0') {
        if (*ptr == '.' || *ptr == '!' || *ptr == '?') {
            // Skip consecutive delimiters and whitespace
            ptr++;
            while (*ptr == ' ' || *ptr == '\n' || *ptr == '\t' || 
                   *ptr == '.' || *ptr == '!' || *ptr == '?') {
                ptr++;
            }
            
            // If there's more content after the delimiter, it's a new sentence
            if (*ptr != '\0') {
                sentence_count++;
            }
        } else {
            ptr++;
        }
    }
    
    return sentence_count;
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

// Connects to NM, sends one message, and disconnects.
// Replace the entire send_async_update_to_nm function with this:

void send_async_update_to_nm(char* message) {
    int temp_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (temp_sock < 0) {
        perror("send_async: socket");
        return;
    }

    struct sockaddr_in nm_addr;
    memset(&nm_addr, 0, sizeof(nm_addr));
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(NM_LISTEN_PORT);
    
    // Use the global variable we saved in main
    if (inet_pton(AF_INET, g_nm_ip, &nm_addr.sin_addr) <= 0) {
        fprintf(stderr, "[SS] ERROR: Invalid NM IP in async update: %s\n", g_nm_ip);
        close(temp_sock);
        return;
    }

    if (connect(temp_sock, (struct sockaddr *) &nm_addr, sizeof(nm_addr)) < 0) {
        // Only print error if it's NOT a "Connection refused" (NM might be down)
        // perror("send_async: connect"); 
        close(temp_sock);
        return;
    }

    // Send the message and immediately close
    send(temp_sock, message, strlen(message), 0);
    close(temp_sock);
}

/*
 * Helper function to find the Nth sentence in a file's content.
 * A sentence is defined by the delimiters ".!?"
 *
 * Parameters:
 * content    - The full string buffer of the file.
 * sent_num   - The sentence number to find (0-indexed).
 * start_ptr  - A (pointer to a char*) to store the start of the sentence.
 * end_ptr    - A (pointer to a char*) to store the end (the delimiter) of the sentence.
 *
 * Returns:
 * true (1) if the sentence was found.
 * false (0) if the sentence number is out of bounds.
 */
bool find_sentence_pointers(char* content, int sent_num, char** start_ptr, char** end_ptr) {
    char* current_start = content;
    int current_sent = 0;

    // Loop until we find the sentence or run out of content
    while (*current_start != '\0') {
        // 1. Skip any leading whitespace (like spaces or newlines)
        while (*current_start == ' ' || *current_start == '\n' || *current_start == '\t') {
            current_start++;
        }
        
        // If we hit the end of the string while skipping, we're done.
        if (*current_start == '\0') {
            return false;
        }

        // 2. Find the end of this sentence
        char* current_end = strpbrk(current_start, ".!?");

        // 3. Check if we found a delimiter
        if (current_end == NULL) {
            // No delimiter found, but there's content. This is the last sentence.
            // Check if this is the sentence we were looking for.
            if (current_sent == sent_num) {
                *start_ptr = current_start;
                // Set the end to the end of the string
                *end_ptr = current_start + strlen(current_start) - 1;
                return true;
            } else {
                // This was the last sentence, but not the one we wanted.
                return false;
            }
        }

        // 4. We found a delimiter. Is this the sentence we want?
        if (current_sent == sent_num) {
            *start_ptr = current_start;
            *end_ptr = current_end;
            return true; // Success!
        }

        // 5. It wasn't. Move to the next sentence.
        current_sent++;
        current_start = current_end + 1; // Move past the delimiter
    }

    // We reached the end of the string without finding the sentence
    return false;
}

/*
 * Helper function to find the Nth word in a specific sentence.
 * A word is defined by the delimiter " " (space).
 *
 * Parameters:
 * sentence_start - Pointer to the start of the sentence (from find_sentence_pointers).
 * sentence_end   - Pointer to the end of the sentence (the delimiter .!?)
 * word_idx       - The word number to find (0-indexed).
 * start_ptr      - A (pointer to a char*) to store the start of the word.
 * end_ptr        - A (pointer to a char*) to store the end (the space) of the word.
 *
 * Returns:
 * true (1) if the word was found.
 * false (0) if the word index is out of bounds.
 */
bool find_word_pointers(char* sentence_start, char* sentence_end, int word_idx, char** start_ptr, char** end_ptr) {
    char* current_start = sentence_start;
    int current_word = 0;

    while (current_start <= sentence_end) {
        // 1. Skip leading spaces (if any)
        while (*current_start == ' ' && current_start <= sentence_end) {
            current_start++;
        }
        
        // If we hit the end of the sentence while skipping, we're done.
        if (current_start > sentence_end) {
            return false;
        }

        // 2. Find the end of this word (the next space)
        char* current_end = strchr(current_start, ' ');

        // 3. Check if we found a space *within* the sentence
        if (current_end == NULL || current_end > sentence_end) {
            // No more spaces. This is the last word.
            if (current_word == word_idx) {
                *start_ptr = current_start;
                // The end of this word is the end of the sentence
                *end_ptr = sentence_end; 
                return true;
            } else {
                return false;
            }
        }

        // 4. We found a space. Is this the word we want?
        if (current_word == word_idx) {
            *start_ptr = current_start;
            *end_ptr = current_end; // The space is the end
            return true;
        }

        // 5. It wasn't. Move to the next word.
        current_word++;
        current_start = current_end + 1; // Move past the space
    }

    return false;
}

// Helper function to ensure directory exists (recursive mkdir)
void ensure_directory_exists(const char* filepath) {
    char path_copy[MAX_PATH_LEN];
    strcpy(path_copy, filepath);
    
    // Remove the filename to get just the dir
    char* last_slash = strrchr(path_copy, '/');
    if (last_slash) {
        *last_slash = '\0'; // Truncate to dir path
        
        char full_dir_path[MAX_PATH_LEN];
        snprintf(full_dir_path, sizeof(full_dir_path), "%s/%s", g_ss_root_path, path_copy);
        
        char cmd[MAX_MSG_LEN];
        // Use mkdir -p. It's the safest, simplest way in C without writing 50 lines of code.
        snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", full_dir_path);
        
        printf("[SS] Creating directory: %s\n", full_dir_path);
        int result = system(cmd);
        if (result != 0) {
            printf("[SS] Warning: mkdir -p returned %d for %s\n", result, full_dir_path);
        }
    }
}

//thread function for a CLIENT connection
void *handle_client_request(void *arg) {
    ClientThreadArgs* args = (ClientThreadArgs*)arg;
    int client_fd = args->client_fd;
    int my_port = args->ss_port;
    free(arg);
    
    char buffer[MAX_MSG_LEN];
    char cmd[MAX_MSG_LEN];
    char filename[MAX_PATH_LEN - sizeof(g_ss_root_path) - 1];
    char log_msg[MAX_MSG_LEN];
    
    memset(buffer, 0, MAX_MSG_LEN);

    // 1. Read the one and only command from the client (e.g., SS_GET_FILE)
    if (read(client_fd, buffer, MAX_MSG_LEN - 1) <= 0) {
        printf("[SS-Client] Client disconnected before sending command.\n");
        close(client_fd);
        return NULL;
    }
    
    sscanf(buffer, "%s %s", cmd, filename);

    if (!is_valid_filename(filename)) {
        send(client_fd, RESP_BAD_REQ "\n", strlen(RESP_BAD_REQ "\n"), 0);
        close(client_fd);
        return NULL;
    }
    
    // 2. Check which command it is
    if (strncmp(cmd, SS_GET_FILE, strlen(SS_GET_FILE)) == 0) {
        
        snprintf(log_msg, MAX_MSG_LEN, "REQ: SS_GET_FILE, FILE: %s", filename);
        log_event(log_msg);
        printf("[SS-Client] Received request for file: %s\n", filename);
        // --- This is your file streaming logic from simpleserver ---
        
        // TODO: This path is hardcoded. You'll make this dynamic later.
        char local_path[MAX_PATH_LEN];
        snprintf(local_path, MAX_PATH_LEN, "%s/%s", g_ss_root_path, filename);
        
        FILE *fp = fopen(local_path, "r");
        if (fp == NULL) {
            perror("fopen failed");
            // We don't send an error, we just close the connection.
            // The client's read() will fail.
        } else {
            char file_buffer[4096];
            size_t bytes_read;
            while ((bytes_read = fread(file_buffer, 1, 4096, fp)) > 0) {
                if (send(client_fd, file_buffer, bytes_read, 0) == -1) {
                    printf("[SS-Client] Client disconnected during file transfer.\n");
                    break;
                }
            }
            fclose(fp);
            printf("[SS-Client] File transfer complete for: %s\n", filename);
        }
    } 
    else if (strncmp(cmd, SS_LOCK, strlen(SS_LOCK)) == 0) {
        char local_path[MAX_PATH_LEN]; // Need to define local_path here
        snprintf(local_path, MAX_PATH_LEN, "%s/%s", g_ss_root_path, filename);

        int sentence_num;
        sscanf(buffer, "%*s %*s %d", &sentence_num);
        snprintf(log_msg, MAX_MSG_LEN, "REQ: SS_LOCK, FILE: %s, SENTENCE: %d", filename, sentence_num);
        log_event(log_msg);
        printf("[SS-Client-Write] Lock requested for %s sen %d\n", filename, sentence_num);
        
        // 1. CHECK IF ALREADY LOCKED
        pthread_mutex_lock(&g_lock_list_mutex);
        bool already_locked = false;
        for (int i = 0; i < g_lock_count; i++) {
            if (strcmp(g_lock_list[i].path, filename) == 0 && 
                g_lock_list[i].sentence_num == sentence_num && 
                g_lock_list[i].is_locked) {
                already_locked = true;
                break;
            }
        }
        
        if (already_locked) {
            pthread_mutex_unlock(&g_lock_list_mutex);
            printf("[SS-Client-Write] Lock FAILED for %s sen %d (already locked)\n", filename, sentence_num);
            send(client_fd, RESP_LOCKED_ERR "\n", strlen(RESP_LOCKED_ERR "\n"), 0);
        } else {
            // 2. OPTIMISTICALLY ACQUIRE LOCK (we'll rollback if validation fails)
            strcpy(g_lock_list[g_lock_count].path, filename);
            g_lock_list[g_lock_count].is_locked = true;
            g_lock_list[g_lock_count].sentence_num = sentence_num;
            g_lock_count++;
            pthread_mutex_unlock(&g_lock_list_mutex);
            
            time_t original_mod_time = 0; // Will store the file's "version"

            // 3. READ FILE TO MEMORY
            char* file_content_buffer = read_file_to_memory(local_path);
            if (file_content_buffer == NULL) {
                if (errno == ENOENT) { // File doesn't exist (new file or deleted)
                    printf("[SS-Client-Write] File %s is new (empty).\n", filename);
                    file_content_buffer = strdup(""); // Allocate empty string
                    original_mod_time = 0; // Set timestamp to 0 for a new file
                } else {
                    // Read error (not just ENOENT)
                    printf("[SS-Client-Write] Error: File not found or unreadable, releasing lock: %s\n", filename);

                    // Release the lock we just acquired
                    pthread_mutex_lock(&g_lock_list_mutex);
                    for (int i = 0; i < g_lock_count; i++) {
                        if (strcmp(g_lock_list[i].path, filename) == 0 && 
                            g_lock_list[i].sentence_num == sentence_num && 
                            g_lock_list[i].is_locked) {
                            g_lock_list[i] = g_lock_list[g_lock_count - 1];
                            g_lock_count--;
                            break;
                        }
                    }
                    pthread_mutex_unlock(&g_lock_list_mutex);

                    send(client_fd, RESP_NOT_FOUND "\n", strlen(RESP_NOT_FOUND "\n"), 0);
                    file_content_buffer = NULL; // Ensure it stays NULL
                }
            } else {
                // File exists, get its timestamp
                struct stat file_stat;
                if (stat(local_path, &file_stat) == 0) {
                    original_mod_time = file_stat.st_mtime;
                }
            }

        // Only proceed if the buffer is valid ---
        if (file_content_buffer != NULL) 
        {
            // 4. VALIDATE SENTENCE INDEX
            int total_sentences = count_sentences(file_content_buffer);
            
            printf("[SS-Client-Write] File %s has %d sentences. Request for sentence %d.\n", 
                   filename, total_sentences, sentence_num);
            
            // Valid indices: 0 to total_sentences-1 (existing) OR total_sentences (append)
            if (sentence_num < 0 || sentence_num > total_sentences) {
                printf("[SS-Client-Write] Lock FAILED for %s. Invalid sentence index %d (file has %d sentences)\n", 
                       filename, sentence_num, total_sentences);
                
                // Send error to client
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                         "%s Sentence index %d out of range (file has %d sentences)\n", 
                         RESP_NOT_FOUND, sentence_num, total_sentences);
                send(client_fd, error_msg, strlen(error_msg), 0);
                
                // ROLLBACK: Remove the lock we just added
                pthread_mutex_lock(&g_lock_list_mutex);
                for (int i = 0; i < g_lock_count; i++) {
                    if (strcmp(g_lock_list[i].path, filename) == 0 && 
                        g_lock_list[i].sentence_num == sentence_num) {
                        // Remove this lock by replacing it with the last one
                        g_lock_list[i] = g_lock_list[g_lock_count - 1];
                        g_lock_count--;
                        break;
                    }
                }
                pthread_mutex_unlock(&g_lock_list_mutex);
                
                free(file_content_buffer);
                // Don't enter transaction loop, just continue to next command
            } else {
                // 5. VALIDATION PASSED - Send LOCKED response
                printf("[SS-Client-Write] Lock ACQUIRED for %s sen %d\n", filename, sentence_num);
                send(client_fd, RESP_LOCKED "\n", strlen(RESP_LOCKED "\n"), 0);
            while (1) { // Transaction loop
                memset(buffer, 0, MAX_MSG_LEN);
                int bytes_read = read(client_fd, buffer, MAX_MSG_LEN - 1);
                if (bytes_read <= 0) {
                    printf("[SS-Client-Write] Client disconnected. Aborting write.\n");
                    pthread_mutex_lock(&g_lock_list_mutex);

                    for (int i = 0; i < g_lock_count; i++) {
                        if (strcmp(g_lock_list[i].path, filename) == 0 && 
                            g_lock_list[i].sentence_num == sentence_num && 
                            g_lock_list[i].is_locked) {
                            g_lock_list[i] = g_lock_list[g_lock_count - 1];
                            g_lock_count--;
                            printf("[SS-Client-Write] Lock RELEASED for sen %d due to disconnect.\n", sentence_num);
                            break;
                        }
                    }
                    pthread_mutex_unlock(&g_lock_list_mutex);
                    free(file_content_buffer); // Free the memory we were editing
                    break; // Client disconnected, abort
                }
            

                sscanf(buffer, "%s", cmd);
                if (strncmp(cmd, SS_UPDATE, strlen(SS_UPDATE)) == 0) {
                    int word_idx;
                    char new_content[MAX_MSG_LEN]; 
                    char cmd_tmp[100];

                    if (sscanf(buffer, "%s %d %[^\n]", cmd_tmp, &word_idx, new_content) < 3) {
                         printf("[SS-Client-Write] Error: Malformed SS_UPDATE (expected 3+ parts, got <3).\n");
                         continue;
                    }
                    
                    char *sent_start, *sent_end, *word_start, *word_end;

                    // --- FIX: Special Case for writing to an empty file ---
                    if (sentence_num == 0 && strlen(file_content_buffer) == 0 && word_idx == 0) {
                        printf("[SS-Client-Write] Special Case: Writing to empty file.\n");
                        
                        char* new_buffer = (char*)malloc(strlen(new_content) + 1);
                        if (new_buffer == NULL) {
                             perror("malloc");
                             continue; // Skip update
                        }
                        strcpy(new_buffer, new_content);
                        
                        free(file_content_buffer);
                        file_content_buffer = new_buffer;
                        
                        printf("[SS-Client-Write] Update successful for sen 0, word 0.\n");
                        
                        continue; // Go to next update
                    }

                    // --- Normal Case: Find the sentence ---
                    if (!find_sentence_pointers(file_content_buffer, sentence_num, &sent_start, &sent_end)) {
                        int sentence_count = 0;
                        char* temp_start = file_content_buffer;
                        while (*temp_start != '\0') {
                             // Skip whitespace
                             while (*temp_start == ' ' || *temp_start == '\n' || *temp_start == '\t') temp_start++;
                             if (*temp_start == '\0') break; // Reached end
                             
                             char* temp_end = strpbrk(temp_start, ".!?");
                             if (temp_end == NULL) {
                                 sentence_count++; // This is the last sentence
                                 break;
                             }
                             sentence_count++;
                             temp_start = temp_end + 1;
                        }

                        // If user wants to write to the *next* new sentence (and is writing word 0)
                        if (sentence_num == sentence_count && word_idx == 0) {
                             printf("[SS-Client-Write] Appending new sentence %d.\n", sentence_num);
                             
                             int old_len = strlen(file_content_buffer);
                             int new_content_len = strlen(new_content);
                             // Need space for: existing content + space/period + new content + null
                             // If old content exists and doesn't end with a delimiter, add one
                             bool needs_delimiter = false;
                             if (old_len > 0) {
                                 char last_char = file_content_buffer[old_len - 1];
                                 if (last_char != '.' && last_char != '!' && last_char != '?') {
                                     needs_delimiter = true;
                                 }
                             }
                             
                             int extra_chars = (old_len > 0 ? 1 : 0) + (needs_delimiter ? 1 : 0); // space and/or delimiter
                             char* new_buffer = (char*)malloc(old_len + extra_chars + new_content_len + 1); 

                             if (new_buffer == NULL) {
                                 perror("malloc");
                                 continue;
                             }
                             
                             // Copy old content, add delimiter if needed, add space, and the new content
                             if (old_len > 0) {
                                 strcpy(new_buffer, file_content_buffer);
                                 if (needs_delimiter) {
                                     new_buffer[old_len] = '.';
                                     new_buffer[old_len + 1] = ' ';
                                     strcpy(new_buffer + old_len + 2, new_content);
                                 } else {
                                     new_buffer[old_len] = ' ';
                                     strcpy(new_buffer + old_len + 1, new_content);
                                 }
                             } else {
                                 strcpy(new_buffer, new_content);
                             }

                             free(file_content_buffer);
                             file_content_buffer = new_buffer;
                             
                             printf("[SS-Client-Write] New sentence append successful.\n");
                             
                             continue; // Go to next update
                        } else {
                             // This is a real error
                             printf("[SS-Client-Write] Error: Sentence %d not found (File has %d sentences).\n", sentence_num, sentence_count);
                             continue; 
                        }
                    }

                    // --- Normal Case: Find the word ---
                    if (!find_word_pointers(sent_start, sent_end, word_idx, &word_start, &word_end)) {
                        
                        // --- FIX: Special Case for appending to a sentence ---
                        // (word_idx is the *next* word index)
                        int word_count = 0;
                        char* temp_start = sent_start;
                        while(temp_start <= sent_end) {
                             while (*temp_start == ' ' && temp_start <= sent_end) temp_start++;
                             if (temp_start > sent_end) break;
                             char* temp_end = strchr(temp_start, ' ');
                             if (temp_end == NULL || temp_end > sent_end) {
                                 word_count++; // Last word
                                 break;
                             }
                             word_count++;
                             temp_start = temp_end + 1;
                        }

                        if (word_idx == word_count) {
                             printf("[SS-Client-Write] Appending to sentence %d (word %d).\n", sentence_num, word_idx);
                             
                             int old_len = strlen(file_content_buffer);
                             int new_content_len = strlen(new_content) + 1; // +1 for the space
                             char* new_buffer = (char*)malloc(old_len + new_content_len + 1); // +1 for null

                             if (new_buffer == NULL) {
                                 perror("malloc");
                                 continue;
                             }
                             
                             // --- NEW SIMPLER LOGIC ---

                             // Part 1: Copy up to (and INCLUDING) the last char of the sentence
                             // (This is the char at sent_end)
                             int part1_len = (sent_end - file_content_buffer) + 1;
                             memcpy(new_buffer, file_content_buffer, part1_len);
                             
                             // Part 2: Add a space and the new word
                             new_buffer[part1_len] = ' ';
                             memcpy(new_buffer + part1_len + 1, new_content, new_content_len - 1);
                             
                             // Part 3: Copy the rest of the file (from AFTER sent_end)
                             int part3_start_offset = part1_len;
                             int part3_len = old_len - part3_start_offset;
                             memcpy(new_buffer + part1_len + new_content_len, file_content_buffer + part3_start_offset, part3_len);
                             
                             // Null-terminate the new string
                             new_buffer[old_len + new_content_len] = '\0';
                             
                             // --- END NEW LOGIC ---
                             
                             free(file_content_buffer);
                             file_content_buffer = new_buffer;
                             
                             printf("[SS-Client-Write] Append successful.\n");
                             
                             // Recompute sentence pointers after buffer reallocation
                             if (!find_sentence_pointers(file_content_buffer, sentence_num, &sent_start, &sent_end)) {
                                 printf("[SS-Client-Write] Warning: Could not recompute sentence pointers after append.\n");
                             }
                             
                             continue; // Done
                        } else {
                             printf("[SS-Client-Write] Error: Word %d not found (count %d) in sentence %d.\n", word_idx, word_count, sentence_num);
                             continue; // Ignore this update
                        }
                    }

                    // --- Normal Case: Inserting a word mid-sentence ---
                    printf("[SS-Client-Write] Inserting at sen %d, word %d.\n", sentence_num, word_idx);
                    
                    int old_len = strlen(file_content_buffer);
                    // We are inserting the new content AND a space
                    int new_content_len = strlen(new_content) + 1; // +1 for the space
                    int new_len = old_len + new_content_len;

                    char* new_buffer = (char*)malloc(new_len + 1);
                    if (new_buffer == NULL) {
                        perror("malloc");
                        continue;
                    }

                    // Part 1: Copy everything *before* the insertion point (word_start)
                    int part1_len = word_start - file_content_buffer;
                    memcpy(new_buffer, file_content_buffer, part1_len);

                    // Part 2: Copy the *new word* and add a space
                    memcpy(new_buffer + part1_len, new_content, new_content_len - 1);
                    new_buffer[part1_len + new_content_len - 1] = ' '; // Add the space

                    // Part 3: Copy everything *from* the insertion point to the end
                    int part3_start_offset = part1_len; // This is the start of the old word
                    int part3_len = old_len - part3_start_offset;
                    memcpy(new_buffer + part1_len + new_content_len, file_content_buffer + part3_start_offset, part3_len);

                    new_buffer[new_len] = '\0'; // Null-terminate

                    free(file_content_buffer); 
                    file_content_buffer = new_buffer; 

                    printf("[SS-Client-Write] Update successful for sen %d, word %d.\n", sentence_num, word_idx);
                    
                    // Recompute sentence pointers after buffer reallocation
                    if (!find_sentence_pointers(file_content_buffer, sentence_num, &sent_start, &sent_end)) {
                        printf("[SS-Client-Write] Warning: Could not recompute sentence pointers after update.\n");
                    }
                
                }
                else if (strncmp(cmd, SS_COMMIT, strlen(SS_COMMIT)) == 0) {
                    snprintf(log_msg, MAX_MSG_LEN, "REQ: SS_COMMIT, FILE: %s", filename);
                    log_event(log_msg); // We'll fix the "UNKNOWN" user later

                    // --- CONFLICT CHECK & MERGE LOGIC ---
                    pthread_mutex_lock(&g_commit_mutex); // Lock the global commit mutex

                    struct stat file_stat;
                    time_t current_mod_time = 0;
                    char bak_path[MAX_PATH_LEN + 5]; // Define bak_path here
                    snprintf(bak_path, MAX_PATH_LEN + 5, "%s.bak", local_path);

                    if (stat(local_path, &file_stat) == 0) {
                        current_mod_time = file_stat.st_mtime;
                    } else if (errno != ENOENT) {
                        perror("stat (commit)"); // Real stat error
                    }
                    // Note: if errno == ENOENT, current_mod_time remains 0. This is fine.

                    if (current_mod_time == original_mod_time) {
                        // --- PATH 1: NO CONFLICT (We are the first to commit) ---
                        printf("[SS-Client-Write] COMMIT received for %s (No conflict).\n", filename);
                        rename(local_path, bak_path); // Create backup
                        
                        FILE *fp = fopen(local_path, "w");
                        if (fp) {
                            fwrite(file_content_buffer, 1, strlen(file_content_buffer), fp);
                            fclose(fp);
                            send(client_fd, RESP_OK "\n", strlen(RESP_OK "\n"), 0);
                            
                            // Send metadata update (using my_port from Task 2)
                            int wc = 0, cc = 0;
                            count_metadata(file_content_buffer, &wc, &cc);
                            char update_msg[MAX_MSG_LEN];
                            snprintf(update_msg, MAX_MSG_LEN, "%s %d %s %d %d\n", S_META_UPDATE, my_port, filename, wc, cc);
                            send_async_update_to_nm(update_msg);
                            
                        } else {
                            perror("fopen (commit)");
                            send(client_fd, RESP_SRV_ERR "\n", strlen(RESP_SRV_ERR "\n"), 0);
                        }

                    } else {
                        // --- PATH 2: CONFLICT (We are the second to commit) ---
                        printf("[SS-Client-Write] COMMIT for %s (Conflict detected, attempting merge).\n", filename);
                        
                        char* my_changes_buffer = file_content_buffer; // Already in memory
                        char* current_file_buffer = read_file_to_memory(local_path); // User A's changes
                        char* original_file_buffer = read_file_to_memory(bak_path); // Common ancestor

                        if (current_file_buffer == NULL) {
                            send(client_fd, RESP_SRV_ERR "\n", strlen(RESP_SRV_ERR "\n"), 0);
                            if(original_file_buffer) free(original_file_buffer);
                            pthread_mutex_unlock(&g_commit_mutex);
                            break; // Break the while loop
                        }

                        if (original_file_buffer == NULL) {
                            printf("[SS-Client-Write] MERGE FAILED for %s (No common ancestor).\n", filename);
                            send(client_fd, RESP_CONFLICT "\n", strlen(RESP_CONFLICT "\n"), 0);
                            free(current_file_buffer);
                            pthread_mutex_unlock(&g_commit_mutex);
                            break; // Break the while loop
                        }

                        // 2. Check for a *true* conflict (did they edit our sentence?)
                        char *original_text_start, *original_text_end;
                        char *their_text_start, *their_text_end;
                        char *my_change_start, *my_change_end;

                        bool original_found = find_sentence_pointers(original_file_buffer, sentence_num, &original_text_start, &original_text_end);
                        bool their_found = find_sentence_pointers(current_file_buffer, sentence_num, &their_text_start, &their_text_end);
                        bool my_found = find_sentence_pointers(my_changes_buffer, sentence_num, &my_change_start, &my_change_end);

                        bool true_conflict = false;
                        
                        if (original_found != their_found) {
                            true_conflict = true; // One person deleted the sentence, the other didn't.
                        } else if (original_found && their_found) {
                            // Both found it, let's compare.
                            int original_len = (original_text_end - original_text_start) + 1;
                            int their_len = (their_text_end - their_text_start) + 1;
                            
                            if (original_len != their_len || strncmp(original_text_start, their_text_start, original_len) != 0) {
                                true_conflict = true; // The sentence we edited has been changed by someone else.
                            }
                        }
                        else if (!original_found && their_found) {
                            true_conflict = true; // They added the sentence we were trying to add.
                        }

                        if (true_conflict) {
                            // 3. Unmergeable Conflict
                            printf("[SS-Client-Write] MERGE FAILED for %s (True conflict on sen %d).\n", filename, sentence_num);
                            send(client_fd, RESP_CONFLICT "\n", strlen(RESP_CONFLICT "\n"), 0);
                        } else {
                            // 4. Perform Merge
                            printf("[SS-Client-Write] MERGE success for %s (Applying changes to sen %d).\n", filename, sentence_num);
                            
                            char* new_merged_buffer;

                            if (!my_found) {
                                // This should not happen (we edited nothing?), but safe to just keep their buffer.
                                new_merged_buffer = strdup(current_file_buffer);
                            } else if (!original_found && !their_found) {
                                // Clean append: We added a new sentence, they didn't.
                                int my_change_len = (my_change_end - my_change_start) + 1;
                                int current_len = strlen(current_file_buffer);
                                
                                // Check if current file needs a delimiter before our new sentence
                                bool needs_delimiter = false;
                                if (current_len > 0) {
                                    char last_char = current_file_buffer[current_len - 1];
                                    if (last_char != '.' && last_char != '!' && last_char != '?') {
                                        needs_delimiter = true;
                                    }
                                }
                                
                                int extra_chars = (current_len > 0 ? 1 : 0) + (needs_delimiter ? 1 : 0);
                                int new_buffer_size = current_len + extra_chars + my_change_len + 1;
                                new_merged_buffer = (char*)malloc(new_buffer_size);
                                
                                if (current_len > 0) {
                                    strcpy(new_merged_buffer, current_file_buffer);
                                    if (needs_delimiter) {
                                        new_merged_buffer[current_len] = '.';
                                        new_merged_buffer[current_len + 1] = ' ';
                                        memcpy(new_merged_buffer + current_len + 2, my_change_start, my_change_len);
                                        new_merged_buffer[current_len + 2 + my_change_len] = '\0';
                                    } else {
                                        new_merged_buffer[current_len] = ' ';
                                        memcpy(new_merged_buffer + current_len + 1, my_change_start, my_change_len);
                                        new_merged_buffer[current_len + 1 + my_change_len] = '\0';
                                    }
                                } else {
                                    memcpy(new_merged_buffer, my_change_start, my_change_len);
                                    new_merged_buffer[my_change_len] = '\0';
                                }

                            } else {
                                // Clean replace: We edited a sentence, they didn't.
                                int my_change_len = (my_change_end - my_change_start) + 1;
                                int their_change_len = (their_text_end - their_text_start) + 1;

                                int new_buffer_size = strlen(current_file_buffer) - their_change_len + my_change_len + 1;
                                new_merged_buffer = (char*)malloc(new_buffer_size);
                                
                                int part1_len = (their_text_start - current_file_buffer);
                                memcpy(new_merged_buffer, current_file_buffer, part1_len);
                                
                                memcpy(new_merged_buffer + part1_len, my_change_start, my_change_len);
                                
                                int part3_start_offset = (their_text_end - current_file_buffer) + 1;
                                int part3_len = strlen(current_file_buffer) - part3_start_offset;
                                memcpy(new_merged_buffer + part1_len + my_change_len, current_file_buffer + part3_start_offset, part3_len);
                                
                                new_merged_buffer[new_buffer_size - 1] = '\0';
                            }

                            // 5. Commit Merged File
                            rename(local_path, bak_path); // Save User A's version as the new backup
                            FILE *fp = fopen(local_path, "w");
                            if (fp) {
                                fwrite(new_merged_buffer, 1, strlen(new_merged_buffer), fp);
                                fclose(fp);
                                send(client_fd, RESP_OK "\n", strlen(RESP_OK "\n"), 0);

                                // Send metadata update (using my_port from Step 2)
                                int wc = 0, cc = 0;
                                count_metadata(new_merged_buffer, &wc, &cc);
                                char update_msg[MAX_MSG_LEN];
                                snprintf(update_msg, MAX_MSG_LEN, "%s %d %s %d %d\n", S_META_UPDATE, my_port, filename, wc, cc);
                                send_async_update_to_nm(update_msg);

                            } else {
                                send(client_fd, RESP_SRV_ERR "\n", strlen(RESP_SRV_ERR "\n"), 0);
                            }
                            free(new_merged_buffer);
                        }
                        
                        // Cleanup
                        free(current_file_buffer);
                        free(original_file_buffer);
                    }
                    
                    pthread_mutex_unlock(&g_commit_mutex); // Unlock the global commit mutex
                    free(file_content_buffer); // Free our in-memory changes
                    break; // Exit transaction loop
                }                    
                }
            } // end while(1)
            
            // 7. RELEASE LOCK (after loop)
            pthread_mutex_lock(&g_lock_list_mutex);
            for (int i = 0; i < g_lock_count; i++) {
                if (strcmp(g_lock_list[i].path, filename) == 0 && 
                    g_lock_list[i].sentence_num == sentence_num && 
                    g_lock_list[i].is_locked) {
                    // "Swap with last" to delete from list
                    g_lock_list[i] = g_lock_list[g_lock_count - 1];
                    g_lock_count--;
                    printf("[SS-Client-Write] Lock RELEASED for %s, sen %d\n", filename, sentence_num);
                    break;
                }
            }
            pthread_mutex_unlock(&g_lock_list_mutex);
            } // end if (file_content_buffer != NULL)
        } // end else (lock not already locked)
    } // end SS_LOCK handler
    else if (strncmp(cmd, SS_GET_STREAM, strlen(SS_GET_STREAM)) == 0) {
        printf("[SS-Client] Received STREAM request for: %s\n", filename);
        snprintf(log_msg, MAX_MSG_LEN, "REQ: SS_GET_STREAM, FILE: %s", filename);
        log_event(log_msg);
        
        char local_path[MAX_PATH_LEN];
        snprintf(local_path, MAX_PATH_LEN, "%s/%s", g_ss_root_path, filename);

        FILE *fp = fopen(local_path, "r");
        if (fp == NULL) {
            perror("[SS-Client] fopen failed for STREAM");
        } else {
            char word_buffer[256];
            // Read one WORD at a time (fscanf handles whitespace)
            while (fscanf(fp, "%255s", word_buffer) == 1) {
                // Send the word with MSG_NOSIGNAL to prevent crash on pipe error
                if (send(client_fd, word_buffer, strlen(word_buffer), MSG_NOSIGNAL) == -1) {
                    printf("[SS-Client] Stream interrupted (Client disconnected).\n");
                    break; 
                }
                // Send a space after the word
                if (send(client_fd, " ", 1, MSG_NOSIGNAL) == -1) {
                    break; 
                }
                
                // The 0.1 second delay
                usleep(STREAM_DELAY_US); 
            }
            fclose(fp);
            printf("[SS-Client] Stream complete for: %s\n", filename);
        }
    }
    else if (strncmp(cmd, SS_VIEWCHECKPOINT, strlen(SS_VIEWCHECKPOINT)) == 0) {
        char tag[MAX_CHECKPOINT_TAG];
        sscanf(buffer, "%*s %s %s", filename, tag);
        
        snprintf(log_msg, MAX_MSG_LEN, "REQ: SS_VIEWCHECKPOINT, FILE: %s, TAG: %s", filename, tag);
        log_event(log_msg);

        char* content = read_checkpoint(filename, tag);
        if (content == NULL) {
            // Checkpoint or file not found
            send(client_fd, RESP_NOT_FOUND "\n", strlen(RESP_NOT_FOUND "\n"), 0);
        } else {
            // Send 200 OK, then the content
            send(client_fd, RESP_OK "\n", strlen(RESP_OK "\n"), 0);
            send(client_fd, content, strlen(content), 0);
            free(content);
        }
    }
    else if (strncmp(cmd, SS_LISTCHECKPOINTS, strlen(SS_LISTCHECKPOINTS)) == 0) {
        sscanf(buffer, "%*s %s", filename);

        snprintf(log_msg, MAX_MSG_LEN, "REQ: SS_LISTCHECKPOINTS, FILE: %s", filename);
        log_event(log_msg);

        char* list = list_checkpoints(filename);
        if (list == NULL) {
            // This indicates a server error (e.g., malloc failed)
            send(client_fd, RESP_SRV_ERR "\n", strlen(RESP_SRV_ERR "\n"), 0);
        } else {
            // Send 200 OK, then the list
            send(client_fd, RESP_OK "\n", strlen(RESP_OK "\n"), 0);
            send(client_fd, list, strlen(list), 0);
            free(list);
        }
    }
    else {
        // Unknown command
        printf("[SS-Client] Unknown command: %s\n", cmd);
    }
    
    close(client_fd);
    return NULL;
}

// --- This is the SERVER part of the SS ---
// It runs in its own thread and just accepts clients
void* run_listener_thread(void* arg) {
    int port = *((int*)arg);
    free(arg);

    int listener_fd;
    struct sockaddr_in ss_server_addr;
    
    // 1. Create the listener socket
    listener_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_fd < 0) {
        perror("SS listener socket() failed");
        pthread_exit(NULL);
    }
    
    // 2. Set SO_REUSEADDR (so you can restart it)
    int opt = 1;
    if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("SS setsockopt() failed");
        pthread_exit(NULL);
    }
    
    // 3. Setup the address struct
    memset(&ss_server_addr, 0, sizeof(ss_server_addr));
    ss_server_addr.sin_family = AF_INET;
    ss_server_addr.sin_addr.s_addr = INADDR_ANY;
    ss_server_addr.sin_port = htons(port);
    
    // 4. Bind
    if (bind(listener_fd, (struct sockaddr *) &ss_server_addr, sizeof(ss_server_addr)) < 0) {
        perror("SS bind() failed");
        pthread_exit(NULL);
    }
    
    // 5. Listen
    if (listen(listener_fd, 10) < 0) {
        perror("SS listen() failed");
        pthread_exit(NULL);
    }
    
    printf("[SS-Listener] SS is now listening for clients on port %d\n", port);
    log_event("Client listener thread started.");

    // 6. The Accept Loop (stolen from simpleserver's main)
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd;
    
    while(1) {
        client_fd = accept(listener_fd, (struct sockaddr *) &client_addr, &client_len);
        if (client_fd < 0) {
            perror("SS accept() failed");
            continue; // Keep listening
        }
        
        printf("[SS-Listener] New client connection accepted.\n");
        
        // --- Spawn a new thread to handle this client's request ---
        pthread_t client_tid;
        ClientThreadArgs* args = malloc(sizeof(ClientThreadArgs));
        args->client_fd = client_fd;
        args->ss_port = port; // 'port' is the argument this function received
        
        if (pthread_create(&client_tid, NULL, handle_client_request, (void *)args) != 0) {
            perror("SS failed to create client handler thread");
            free(args);
            close(client_fd);
        }
    }
    
    // This part is never reached
    close(listener_fd);
    return NULL;
}

/*
 * ============================================================================
 * CHECKPOINT HELPER FUNCTIONS
 * ============================================================================
 */

/*
 * Creates a checkpoint by copying the current file to a checkpoint file
 * Checkpoint files are stored as: <filename>.checkpoint.<tag>
 * Returns true on success, false on failure
 */
bool create_checkpoint(const char* filename, const char* tag) {
    char source_path[MAX_PATH_LEN];
    char checkpoint_path[MAX_PATH_LEN];
    
    snprintf(source_path, MAX_PATH_LEN, "%s/%s", g_ss_root_path, filename);
    snprintf(checkpoint_path, MAX_PATH_LEN, "%s/%s.checkpoint.%s", 
             g_ss_root_path, filename, tag);
    
    // Check if source file exists
    FILE* source = fopen(source_path, "r");
    if (source == NULL) {
        perror("create_checkpoint: source file not found");
        return false;
    }
    
    // Create checkpoint file
    FILE* checkpoint = fopen(checkpoint_path, "w");
    if (checkpoint == NULL) {
        perror("create_checkpoint: failed to create checkpoint");
        fclose(source);
        return false;
    }
    
    // Copy content
    char buffer[4096];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), source)) > 0) {
        if (fwrite(buffer, 1, bytes, checkpoint) != bytes) {
            perror("create_checkpoint: write failed");
            fclose(source);
            fclose(checkpoint);
            remove(checkpoint_path);
            return false;
        }
    }
    
    fclose(source);
    fclose(checkpoint);
    
    // Add to checkpoint list
    pthread_mutex_lock(&g_checkpoint_mutex);
    
    // Check if checkpoint with this tag already exists for this file
    for (int i = 0; i < g_checkpoint_count; i++) {
        if (strcmp(g_checkpoint_list[i].filename, filename) == 0 &&
            strcmp(g_checkpoint_list[i].tag, tag) == 0) {
            // Update existing checkpoint
            g_checkpoint_list[i].created_at = time(NULL);
            strcpy(g_checkpoint_list[i].checkpoint_path, checkpoint_path);
            pthread_mutex_unlock(&g_checkpoint_mutex);
            return true;
        }
    }
    
    // Add new checkpoint
    if (g_checkpoint_count < MAX_CHECKPOINTS) {
        strcpy(g_checkpoint_list[g_checkpoint_count].tag, tag);
        strcpy(g_checkpoint_list[g_checkpoint_count].filename, filename);
        strcpy(g_checkpoint_list[g_checkpoint_count].checkpoint_path, checkpoint_path);
        g_checkpoint_list[g_checkpoint_count].created_at = time(NULL);
        g_checkpoint_count++;
    } else {
        pthread_mutex_unlock(&g_checkpoint_mutex);
        remove(checkpoint_path);
        return false;
    }
    
    pthread_mutex_unlock(&g_checkpoint_mutex);
    return true;
}

/*
 * Reads and returns the content of a checkpoint
 * Caller must free() the returned string
 */
char* read_checkpoint(const char* filename, const char* tag) {
    pthread_mutex_lock(&g_checkpoint_mutex);
    
    char checkpoint_path[MAX_PATH_LEN] = "";
    bool found = false;
    
    for (int i = 0; i < g_checkpoint_count; i++) {
        if (strcmp(g_checkpoint_list[i].filename, filename) == 0 &&
            strcmp(g_checkpoint_list[i].tag, tag) == 0) {
            strcpy(checkpoint_path, g_checkpoint_list[i].checkpoint_path);
            found = true;
            break;
        }
    }
    
    pthread_mutex_unlock(&g_checkpoint_mutex);
    
    if (!found) {
        return NULL;
    }
    
    return read_file_to_memory(checkpoint_path);
}

/*
 * Reverts a file to a checkpoint by copying the checkpoint file
 * back to the original file location
 */
bool revert_to_checkpoint(const char* filename, const char* tag) {
    char current_path[MAX_PATH_LEN];
    char checkpoint_path[MAX_PATH_LEN];
    
    snprintf(current_path, MAX_PATH_LEN, "%s/%s", g_ss_root_path, filename);
    
    pthread_mutex_lock(&g_checkpoint_mutex);
    
    bool found = false;
    for (int i = 0; i < g_checkpoint_count; i++) {
        if (strcmp(g_checkpoint_list[i].filename, filename) == 0 &&
            strcmp(g_checkpoint_list[i].tag, tag) == 0) {
            strcpy(checkpoint_path, g_checkpoint_list[i].checkpoint_path);
            found = true;
            break;
        }
    }
    
    pthread_mutex_unlock(&g_checkpoint_mutex);
    
    if (!found) {
        return false;
    }
    
    // Check if file is locked
    pthread_mutex_lock(&g_lock_list_mutex);
    bool is_locked = false;
    for (int i = 0; i < g_lock_count; i++) {
        if (strcmp(g_lock_list[i].path, filename) == 0 && g_lock_list[i].is_locked) {
            is_locked = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock_list_mutex);
    
    if (is_locked) {
        return false; // Can't revert while file is locked
    }
    
    // Copy checkpoint to current file
    FILE* checkpoint = fopen(checkpoint_path, "r");
    if (checkpoint == NULL) {
        return false;
    }
    
    FILE* current = fopen(current_path, "w");
    if (current == NULL) {
        fclose(checkpoint);
        return false;
    }
    
    char buffer[4096];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), checkpoint)) > 0) {
        fwrite(buffer, 1, bytes, current);
    }
    
    fclose(checkpoint);
    fclose(current);
    
    return true;
}

/*
 * Lists all checkpoints for a given file
 * Returns a dynamically allocated string containing the list
 * Caller must free() the returned string
 */
char* list_checkpoints(const char* filename) {
    pthread_mutex_lock(&g_checkpoint_mutex);
    
    // Allocate a large buffer for the list
    char* list = (char*)malloc(4096);
    if (list == NULL) {
        pthread_mutex_unlock(&g_checkpoint_mutex);
        return NULL;
    }
    
    strcpy(list, "");
    int count = 0;
    
    for (int i = 0; i < g_checkpoint_count; i++) {
        if (strcmp(g_checkpoint_list[i].filename, filename) == 0) {
            char time_str[100];
            struct tm ltime;
            localtime_r(&g_checkpoint_list[i].created_at, &ltime);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &ltime);
            
            char line[256];
            snprintf(line, sizeof(line), "%s | %s\n", 
                     g_checkpoint_list[i].tag, time_str);
            strcat(list, line);
            count++;
        }
    }
    
    pthread_mutex_unlock(&g_checkpoint_mutex);
    
    if (count == 0) {
        strcpy(list, "No checkpoints found.\n");
    }
    
    return list;
}

int main(int argc, char* argv[]) {
    
    g_log_fp = fopen(SS_LOG_FILE, "a"); // "a" for append
    if (g_log_fp == NULL) {
        perror("FATAL: Failed to open log file");
        exit(1);
    }
    log_event("--- Storage Server Started ---");
    
    // UPDATED ARGUMENT PARSING FOR MULTI-MACHINE SUPPORT
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <client-facing-port> <nm-ip> <my-ip>\n", argv[0]);
        fprintf(stderr, "Example: ./bin/storage_server 9002 192.168.1.10 192.168.1.20\n");
        exit(1);
    }

    int client_port_for_ss = atoi(argv[1]);
    char* nm_ip_arg = argv[2];    // IP of the Name Server
    char* my_ip_arg = argv[3];    // IP of THIS Storage Server (accessible by others)
    strcpy(g_nm_ip, nm_ip_arg); // Save NM IP for later use 
    snprintf(g_ss_root_path, sizeof(g_ss_root_path), "ss_data/%d", client_port_for_ss);

    // 2. Create parent directory "ss_data"
    // We check for -1 AND if the error is *not* "File Exists"
    if (mkdir("ss_data", 0777) == -1 && errno != EEXIST) {
        perror("FATAL: Failed to create 'ss_data' directory");
        exit(1);
    }
    
    // 3. Create the specific server directory (e.g., "ss_data/9002")
    if (mkdir(g_ss_root_path, 0777) == -1 && errno != EEXIST) {
        perror("FATAL: Failed to create server-specific directory");
        exit(1);
    }
    
    printf("[SS] Storage root path set to: %s\n", g_ss_root_path);
    
    // --- CLIENT PART: Connect to Name Server ---
    printf("SS starting... connecting to Name Server at %s...\n", nm_ip_arg);
    
    struct sockaddr_in nm_addr;

    g_nm_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_nm_fd < 0) {
        perror("SS socket() to NM failed");
        exit(1);
    }

    memset(&nm_addr, 0, sizeof(nm_addr));
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(NM_LISTEN_PORT);
    
    // USE THE COMMAND LINE ARGUMENT FOR NM IP
    if (inet_pton(AF_INET, nm_ip_arg, &nm_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid Name Server IP address: %s\n", nm_ip_arg);
        exit(1);
    }

    if (connect(g_nm_fd, (struct sockaddr *) &nm_addr, sizeof(nm_addr)) < 0) {
        perror("SS connect() to NM failed");
        exit(1);
    }

    printf("1. Connected to Name Server successfully.\n");
    
    // --- REGISTRATION PART ---
    char reg_buffer[MAX_MSG_LEN];
    memset(reg_buffer, 0, MAX_MSG_LEN);
    
    // USE THE COMMAND LINE ARGUMENT FOR OWN IP
    // S_INIT <my_ip> <nm_port> <client_port>
    sprintf(reg_buffer, "%s %s %d %d\n", S_INIT, my_ip_arg, 0, client_port_for_ss);
    printf("Sending registration: %s", reg_buffer);
    write(g_nm_fd, reg_buffer, strlen(reg_buffer));
    
    // Wait for the "OK" from the NM
    memset(reg_buffer, 0, MAX_MSG_LEN);
    if (read(g_nm_fd, reg_buffer, MAX_MSG_LEN - 1) <= 0) {
        perror("NM closed connection during registration");
        exit(1);
    }
    
    if (strncmp(reg_buffer, RESP_OK, strlen(RESP_OK)) != 0) {
        printf("Name Server rejected registration: %s\n", reg_buffer);
        exit(1);
    }
    
    printf("2. Registered with Name Server successfully.\n");
    log_event("Registered with Name Server");
    
    // --- SERVER PART ---
    printf("3. Spawning client-listener thread...\n");
    
    pthread_t listener_tid;
    
    // We must pass the port number to the new thread
    // We must use malloc to avoid a race condition
    int *port_arg = malloc(sizeof(int));
    *port_arg = client_port_for_ss;

    if (pthread_create(&listener_tid, NULL, run_listener_thread, (void *)port_arg) != 0) {
        perror("Failed to create listener thread");
        exit(1);
    }

    printf("SS initialization complete. Main thread is now waiting for NM commands.\n");

    // --- NM COMMAND LOOP ---
    char nm_buffer[MAX_MSG_LEN];
    char cmd[MAX_MSG_LEN];
    char filename[MAX_PATH_LEN - sizeof(g_ss_root_path) - 1];
    char local_path[MAX_PATH_LEN];
    char resp_buf[MAX_MSG_LEN]; // Declare all buffers outside the loop

    while(1) {
        // Clear all buffers for this new command
        char log_msg[MAX_MSG_LEN];
        memset(nm_buffer, 0, MAX_MSG_LEN);
        memset(cmd, 0, MAX_MSG_LEN);
        memset(filename, 0, sizeof(filename));
        memset(local_path, 0, MAX_PATH_LEN);
        memset(resp_buf, 0, MAX_MSG_LEN);
        
        int bytes_read = read(g_nm_fd, nm_buffer, MAX_MSG_LEN - 1);
        
        if (bytes_read <= 0) {
            printf("Name Server disconnected. Exiting.\n");
            exit(1); // If NM dies, SS should die
        }
        
        // printf("Received command from NM: %s", nm_buffer);
        
        sscanf(nm_buffer, "%s %s", cmd, filename);

        if (strncmp(cmd, NM_HEARTBEAT, strlen(NM_HEARTBEAT)) == 0) {
            // Heartbeat request from Name Server (no response needed - fire and forget)
            // printf("[HEARTBEAT] Received heartbeat from NM\n"); // Optional
            continue; // Skip the rest of the loop, do NOT send a response
        }

        if (!is_valid_filename(filename)) {
            snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
            write(g_nm_fd, resp_buf, strlen(resp_buf)); 
            continue; // Skip to next command
        }

        if (strncmp(cmd, NM_CREATE, strlen(NM_CREATE)) == 0) {
            snprintf(log_msg, MAX_MSG_LEN, "REQ: NM_CREATE, FILE: %s", filename);
            log_event(log_msg);
            printf("[SS-NM Loop] Received NM_CREATE for: %s\n", filename);
            
            // --- Ensure parent directory exists ---
            ensure_directory_exists(filename); 
            // ---------------------
            
            snprintf(local_path, MAX_PATH_LEN, "%s/%s", g_ss_root_path, filename);
            printf("[SS-NM Loop] Full path: %s\n", local_path);

            FILE *fp = fopen(local_path, "w");
            if (fp == NULL) {
                perror("[SS-NM Loop] fopen failed");
                printf("[SS-NM Loop] Failed to create: %s\n", local_path);
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
            } else {
                fclose(fp);
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_OK);

                // printf("[SS-NM Loop] SIMULATING SLOW CREATE...\n");
                // sleep(5); // Simulate a 5-second disk write

                printf("[SS-NM Loop] Successfully created file: %s\n", filename);
            }
        } 
        else if (strncmp(cmd, NM_DELETE, strlen(NM_DELETE)) == 0) {
            snprintf(log_msg, MAX_MSG_LEN, "REQ: NM_DELETE, FILE: %s", filename);
            log_event(log_msg);
            printf("[SS-NM Loop] Received NM_DELETE for: %s\n", filename);
            snprintf(local_path, MAX_PATH_LEN, "%s/%s", g_ss_root_path, filename);

            pthread_mutex_lock(&g_lock_list_mutex);
            bool is_file_locked = false;
            for (int i = 0; i < g_lock_count; i++) {
                if (strcmp(g_lock_list[i].path, filename) == 0 && g_lock_list[i].is_locked) {
                    is_file_locked = true;
                    break;
                }
            }
            pthread_mutex_unlock(&g_lock_list_mutex);

            if (is_file_locked) {
                printf("[SS-NM Loop] DELETE failed for %s (file is locked by a user).\n", filename);
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_LOCKED_ERR);
            } else if (remove(local_path) == 0) {
                printf("[SS-NM Loop] Successfully deleted file: %s\n", filename);
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_OK);
            } else {
                perror("[SS-NM Loop] remove() failed");
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
            }
        }
        else if (strncmp(cmd, NM_UNDO, strlen(NM_UNDO)) == 0) {
            snprintf(log_msg, MAX_MSG_LEN, "REQ: NM_UNDO, FILE: %s", filename);
            log_event(log_msg);
            printf("[SS-NM Loop] Received NM_UNDO for: %s\n", filename);
            snprintf(local_path, MAX_PATH_LEN, "%s/%s", g_ss_root_path, filename);
            char bak_path[MAX_PATH_LEN + 5];
            snprintf(bak_path, MAX_PATH_LEN+5, "%s.bak", local_path);

            pthread_mutex_lock(&g_lock_list_mutex);
            bool is_file_locked = false;
            for (int i = 0; i < g_lock_count; i++) {
                // If *any* sentence is locked for this file, block the undo.
                if (strcmp(g_lock_list[i].path, filename) == 0 && g_lock_list[i].is_locked) {
                    is_file_locked = true;
                    break;
                }
            }
            pthread_mutex_unlock(&g_lock_list_mutex);

            if (is_file_locked) {
                printf("[SS-NM Loop] UNDO failed for %s (file is locked by a user).\n", filename);
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_LOCKED_ERR);
            } else {

                // This is the core logic: swap the .bak file back to the original
                if (rename(bak_path, local_path) == 0) {
                    printf("[SS-NM Loop] Successfully reverted file: %s\n", filename);
                    snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_OK);
                } else {
                    perror("[SS-NM Loop] rename(undo) failed");
                    // This happens if there's no .bak file
                    snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_NOT_FOUND); 
                }
            }
        }
        else if (strncmp(cmd, NM_CHECKPOINT, strlen(NM_CHECKPOINT)) == 0) {
            char tag[MAX_CHECKPOINT_TAG];
            sscanf(nm_buffer, "%*s %s %s", filename, tag);
            
            snprintf(log_msg, MAX_MSG_LEN, "REQ: NM_CHECKPOINT, FILE: %s, TAG: %s", filename, tag);
            log_event(log_msg);
            printf("[SS-NM Loop] Received NM_CHECKPOINT for: %s (tag: %s)\n", filename, tag);
            
            if (!is_valid_filename(filename)) {
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
                write(g_nm_fd, resp_buf, strlen(resp_buf));
                continue;
            }
            
            snprintf(local_path, MAX_PATH_LEN, "%s/%s", g_ss_root_path, filename);
            
            // Check if file exists
            if (access(local_path, F_OK) != 0) {
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_NOT_FOUND);
            } else if (create_checkpoint(filename, tag)) {
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_OK);
                printf("[SS-NM Loop] Checkpoint created: %s (tag: %s)\n", filename, tag);
            } else {
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
            }
            
            write(g_nm_fd, resp_buf, strlen(resp_buf));
            continue;
        }
        else if (strncmp(cmd, NM_REVERT, strlen(NM_REVERT)) == 0) {
            char tag[MAX_CHECKPOINT_TAG];
            sscanf(nm_buffer, "%*s %s %s", filename, tag);
            
            snprintf(log_msg, MAX_MSG_LEN, "REQ: NM_REVERT, FILE: %s, TAG: %s", filename, tag);
            log_event(log_msg);
            printf("[SS-NM Loop] Received NM_REVERT for: %s (tag: %s)\n", filename, tag);
            
            if (!is_valid_filename(filename)) {
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
                write(g_nm_fd, resp_buf, strlen(resp_buf));
                continue;
            }
            
            if (revert_to_checkpoint(filename, tag)) {
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_OK);
                printf("[SS-NM Loop] File reverted: %s (tag: %s)\n", filename, tag);
            } else {
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_LOCKED_ERR);
            }
            
            write(g_nm_fd, resp_buf, strlen(resp_buf));
            continue;
        }
        else if (strncmp(cmd, NM_HEARTBEAT, strlen(NM_HEARTBEAT)) == 0) {
            // Heartbeat request from Name Server (no response needed - fire and forget)
            printf("[HEARTBEAT] Received heartbeat from NM\n");
            continue;
        }
        else if (strncmp(cmd, NM_LIST_FILES, strlen(NM_LIST_FILES)) == 0) {
            // List all files stored on this server
            printf("[RECOVERY] Received NM_LIST_FILES request\n");
            
            snprintf(resp_buf, MAX_MSG_LEN, "S_FILE_LIST\n");
            write(g_nm_fd, resp_buf, strlen(resp_buf));
            
            // Recursively list all files in storage directory
            DIR* dir = opendir(g_ss_root_path);
            if (dir) {
                struct dirent* entry;
                while ((entry = readdir(dir)) != NULL) {
                    // Skip . and .. directories
                    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                        continue;
                    }
                    
                    // Check if it's a regular file using stat
                    char full_path[MAX_PATH_LEN];
                    snprintf(full_path, MAX_PATH_LEN, "%s/%s", g_ss_root_path, entry->d_name);
                    struct stat st;
                    if (stat(full_path, &st) == 0 && S_ISREG(st.st_mode)) {
                        snprintf(resp_buf, MAX_MSG_LEN, "%s\n", entry->d_name);
                        write(g_nm_fd, resp_buf, strlen(resp_buf));
                    }
                }
                closedir(dir);
            }
            
            snprintf(resp_buf, MAX_MSG_LEN, "S_FILE_LIST_END\n");
            write(g_nm_fd, resp_buf, strlen(resp_buf));
            continue;
        }
        else if (strncmp(cmd, NM_SYNC, strlen(NM_SYNC)) == 0) {
            // Sync a file from another storage server
            char source_ip[INET_ADDRSTRLEN];
            int source_port;
            sscanf(nm_buffer, "%*s %s %s %d", filename, source_ip, &source_port);
            
            printf("[RECOVERY] Syncing '%s' from %s:%d\n", filename, source_ip, source_port);
            
            // Connect to source SS
            int source_fd = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in source_addr;
            memset(&source_addr, 0, sizeof(source_addr));
            source_addr.sin_family = AF_INET;
            source_addr.sin_port = htons(source_port);
            inet_pton(AF_INET, source_ip, &source_addr.sin_addr);
            
            if (connect(source_fd, (struct sockaddr*)&source_addr, sizeof(source_addr)) == 0) {
                // Request file
                char sync_cmd[MAX_MSG_LEN];
                snprintf(sync_cmd, MAX_MSG_LEN, "SS_GET_FILE %s\n", filename);
                write(source_fd, sync_cmd, strlen(sync_cmd));
                
                // Read file content and save locally
                snprintf(local_path, MAX_PATH_LEN, "%s/%s", g_ss_root_path, filename);
                ensure_directory_exists(filename);
                
                FILE* local_fp = fopen(local_path, "w");
                if (local_fp) {
                    char file_buf[4096];
                    ssize_t bytes;
                    while ((bytes = read(source_fd, file_buf, sizeof(file_buf))) > 0) {
                        fwrite(file_buf, 1, bytes, local_fp);
                    }
                    fclose(local_fp);
                    printf("[RECOVERY] Successfully synced '%s'\n", filename);
                    snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_OK);
                } else {
                    snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
                }
                close(source_fd);
            } else {
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
            }
            
            write(g_nm_fd, resp_buf, strlen(resp_buf));
            continue;
        }
        else if (strncmp(cmd, NM_RENAME, strlen(NM_RENAME)) == 0) {
            char new_path[MAX_PATH_LEN];
            sscanf(nm_buffer, "%*s %s %s", filename, new_path);
            
            snprintf(log_msg, MAX_MSG_LEN, "REQ: NM_RENAME, OLD: %s, NEW: %s", filename, new_path);
            log_event(log_msg);
            
            char old_full_path[MAX_PATH_LEN];
            char new_full_path[MAX_PATH_LEN];
            snprintf(old_full_path, MAX_PATH_LEN, "%s/%s", g_ss_root_path, filename);
            snprintf(new_full_path, MAX_PATH_LEN, "%s/%s", g_ss_root_path, new_path);
            
            // Ensure destination folder exists before moving
            ensure_directory_exists(new_path);
            
            if (rename(old_full_path, new_full_path) == 0) {
                printf("Renamed %s to %s\n", filename, new_path);
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_OK);
            } else {
                perror("rename failed");
                snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_SRV_ERR);
            }
            
            write(g_nm_fd, resp_buf, strlen(resp_buf)); // Send ACK
            continue;
        }
        else {
            printf("[SS-NM Loop] Unknown NM command: %s\n", cmd);
            snprintf(resp_buf, MAX_MSG_LEN, "%s\n", RESP_BAD_REQ);
        }
        
        // Send the single, formatted response
        write(g_nm_fd, resp_buf, strlen(resp_buf)); 
    }       

    close(g_nm_fd);
    return 0;
}
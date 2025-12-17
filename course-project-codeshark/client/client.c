#include "protocol.h"
#include <stdio.h>      // For printf, fgets
#include <stdlib.h>     // For exit, EXIT_FAILURE
#include <string.h>     // For strlen, strtok, memset
#include <unistd.h>     // For close
#include <arpa/inet.h>  // For sockaddr_in, inet_pton
#include <sys/socket.h> // For socket, connect
#include <stdbool.h>
#include <errno.h>      // For errno, EWOULDBLOCK, EAGAIN
#include <sys/time.h>   // For gettimeofday

#include "config.h"  // Add this at the top with other includes

// REMOVE the hardcoded #define NM_IP line
// Use NM_HOST from config.h instead

void print_error(const char *code)
{
    if (!code)
        return;
    if (!strncmp(code, "400", 3))
        printf("Error: Bad request. Check your syntax.\n");
    else if (!strncmp(code, "403", 3))
        printf("Error: Access denied.\n");
    else if (!strncmp(code, "404", 3))
        printf("Error: File not found.\n");
    else if (!strncmp(code, "409", 3))
        printf("Error: Conflict. File was updated by another user. Your changes were not saved.\n");
    else if (!strncmp(code, "503", 3))
        printf("Error: Storage server unavailable.\n");
    else if (!strncmp(code, "504", 3))
        printf("Error: Sentence locked by another user.\n");
    else
        printf("Unexpected response: %s\n", code);
}

int main(int argc, char* argv[])
{
    int sockfd;
    struct sockaddr_in nm_addr;
    char username[100];
    char init_msg[120];

    // UPDATED ARGUMENT PARSING
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <nm-ip>\n", argv[0]);
        fprintf(stderr, "Example: ./bin/client 192.168.1.10\n");
        exit(1);
    }
    char* nm_ip_arg = argv[1];

    // Prompt the user to enter their username
    printf("Enter username: ");
    fgets(username, sizeof(username), stdin);
    // Remove trailing newline character from username, if present
    size_t len = strlen(username);
    if (len > 0 && username[len - 1] == '\n')
    {
        username[len - 1] = '\0';
    }

    // Create a TCP socket for IPv4 communication
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("Failed to create socket");
        exit(EXIT_FAILURE);
    }

    // Set up the server address structure for the Name Server
    memset(&nm_addr, 0, sizeof(nm_addr));
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(NM_LISTEN_PORT);
    
    // USE ARGUMENT IP
    if (inet_pton(AF_INET, nm_ip_arg, &nm_addr.sin_addr) <= 0)
    {
        fprintf(stderr, "Invalid Name Server IP: %s\n", nm_ip_arg);
        exit(EXIT_FAILURE);
    }

    // Connect to the Name Server at NM_IP:NM_LISTEN_PORT
    if (connect(sockfd, (struct sockaddr *)&nm_addr, sizeof(nm_addr)) < 0)
    {
        perror("Connection to Name Server failed");
        exit(EXIT_FAILURE);
    }

    // Format and send the INIT_CLIENT <username> message to register this client
    snprintf(init_msg, sizeof(init_msg), "%s %s\n", C_INIT, username);
    if (send(sockfd, init_msg, strlen(init_msg), 0) < 0)
    {
        perror("Failed to send INIT_CLIENT message");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    printf("Registered with Name Server as '%s'.\n", username);

    char response_buf[MAX_MSG_LEN];
    memset(response_buf, 0, MAX_MSG_LEN);
    int n = recv(sockfd, response_buf, MAX_MSG_LEN - 1, 0);
    if (n <= 0)
    {
        perror("Failed to receive registration ACK from NM");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    response_buf[n] = '\0';
    if (strncmp(response_buf, RESP_OK, 3) != 0)
    {
        printf("Error: Name Server rejected registration: %s", response_buf);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Command input loop for user commands (CREATE, READ, EXIT)
    char cmdline[256];  // Buffer for user input
    char request[256];  // Buffer for request sent to NM
    char response[256]; // Buffer for response from NM

    while (1)
    {
        printf("> ");
        if (fgets(cmdline, sizeof(cmdline), stdin) == NULL)
        {
            printf("Input error or EOF. Exiting.\n");
            break;
        }
        size_t clen = strlen(cmdline);
        if (clen > 0 && cmdline[clen - 1] == '\n')
        {
            cmdline[clen - 1] = '\0';
        }
        char *cmd = strtok(cmdline, " ");
        if (!cmd)
            continue;

        // ----- CREATE command (already implemented) -----
        if (strcmp(cmd, "CREATE") == 0)
        {
            char *filename = strtok(NULL, " ");
            if (!filename)
            {
                printf("Error: No filename given.\n");
                continue;
            }
            snprintf(request, sizeof(request), "%s %s\n", C_REQ_CREATE, filename);
            if (send(sockfd, request, strlen(request), 0) < 0)
            {
                perror("Failed to send REQ_CREATE");
                continue;
            }
            int n = recv(sockfd, response, sizeof(response) - 1, 0);
            if (n < 0)
            {
                perror("Failed to receive NM response");
                continue;
            }
            response[n] = '\0';
            if (strncmp(response, "200", 3) == 0)
            {
                printf("File created successfully.\n");
            }
            else if (strncmp(response, "409", 3) == 0)
            {
                printf("Error: File already exists.\n");
            }
            else if (strncmp(response, "400", 3) == 0)
            {
                printf("Error: Bad request.\n");
            }
            else
            {
                printf("Unexpected response: %s\n", response);
            }
        }
        // ----- READ command -----
        else if (strcmp(cmd, "READ") == 0) {
            char *filename = strtok(NULL, " ");
            if (!filename) {
                printf("Error: No filename given.\n");
                continue;
            }
            // Step 1: Request file location from NM
            snprintf(request, sizeof(request), "%s %s\n", C_REQ_READ, filename);
            if (send(sockfd, request, strlen(request), 0) < 0) {
                perror("Failed to send REQ_READ");
                continue;
            }
            int n = recv(sockfd, response, sizeof(response)-1, 0);
            if (n < 0) {
                perror("Failed to receive NM response");
                continue;
            }
            response[n] = '\0';

            char ip[100], port_str[20];
            // Step 2: Handle NM’s response
            // --- THIS IS THE NEW, CLEANER LOGIC ---
            if (strncmp(response, RESP_SS_INFO, 3) == 0) {
                // Success! Parse the SS info
                if (sscanf(response, "%*s %s %s", ip, port_str) != 2) {
                    printf("Error: Invalid NM response: %s\n", response);
                    continue;
                }
                int ss_port = atoi(port_str);

                // Step 3: Connect to Storage Server
                int ss_sock = socket(AF_INET, SOCK_STREAM, 0);
                if (ss_sock < 0) {
                    perror("Failed to create socket to Storage Server");
                    continue;
                }
                struct sockaddr_in ss_addr;
                memset(&ss_addr, 0, sizeof(ss_addr));
                ss_addr.sin_family = AF_INET;
                ss_addr.sin_port = htons(ss_port);
                if (inet_pton(AF_INET, ip, &ss_addr.sin_addr) <= 0) {
                    perror("Invalid Storage Server address");
                    close(ss_sock);
                    continue;
                }
                if (connect(ss_sock, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) < 0) {
                    perror("Connection to Storage Server failed");
                    close(ss_sock);
                    continue;
                }

                // Step 4: Send GET_FILE <filename> to SS
                snprintf(request, sizeof(request), "%s %s\n", SS_GET_FILE, filename);
                if (send(ss_sock, request, strlen(request), 0) < 0) {
                    perror("Failed to send GET_FILE");
                    close(ss_sock);
                    continue;
                }

                // Step 5: Receive and print file content from SS
                printf("===== File: %s =====\n", filename);
                
                // --- THIS FIXES THE 'xx' BUG ---
                memset(response, 0, sizeof(response)); 
                
                while (1) {
                    int bytes_recvd = recv(ss_sock, response, sizeof(response)-1, 0);
                    if (bytes_recvd <= 0) break; // 0 = closed, <0 = error
                    response[bytes_recvd] = '\0';
                    printf("%s", response); // Print as received
                }
                printf("\n===== End of file =====\n");
                close(ss_sock);
            } else {
                // --- THIS FIXES THE '403' BUG ---
                print_error(response);
            }
        }
        // ----- ADDACCESS -----
        else if (strcmp(cmd, "ADDACCESS") == 0)
        {
            char *perm_str = strtok(NULL, " "); 
            char *filename = strtok(NULL, " "); 
            char *user = strtok(NULL, " ");     

            if (!perm_str || !filename || !user)
            {
                printf("Usage: ADDACCESS <-R|-W> <filename> <username>\n");
                continue;
            }
            snprintf(request, sizeof(request), "%s %s %s %s\n",
                     C_REQ_ADD_ACC, perm_str, filename, user);
            send(sockfd, request, strlen(request), 0);
            int n = recv(sockfd, response, sizeof(response) - 1, 0);
            if (n <= 0)
            {
                perror("recv");
                continue;
            }
            response[n] = '\0';
            if (!strncmp(response, "200", 3))
                printf("Access granted successfully.\n");
            else if (!strncmp(response, "403", 3))
                printf("Error: You are not the owner.\n");
            else
                printf("Unexpected response: %s\n", response);
        }

        // ----- REMACCESS -----
        else if (strcmp(cmd, "REMACCESS") == 0)
        {
            char *filename = strtok(NULL, " ");
            char *user = strtok(NULL, " ");
            if (!filename || !user)
            {
                printf("Usage: REMACCESS <filename> <username>\n");
                continue;
            }
            snprintf(request, sizeof(request), "%s %s %s\n",
                     C_REQ_REM_ACC, filename, user);
            send(sockfd, request, strlen(request), 0);
            int n = recv(sockfd, response, sizeof(response) - 1, 0);
            if (n <= 0)
            {
                perror("recv");
                continue;
            }
            response[n] = '\0';
            if (!strncmp(response, "200", 3))
                printf("Access removed successfully.\n");
            else if (!strncmp(response, "403", 3))
                printf("Error: You are not the owner.\n");
            else
                printf("Unexpected response: %s\n", response);
        }

        // ----- DELETE -----
        else if (strcmp(cmd, "DELETE") == 0)
        {
            char *filename = strtok(NULL, " ");
            if (!filename)
            {
                printf("Usage: DELETE <filename>\n");
                continue;
            }
            snprintf(request, sizeof(request), "%s %s\n", C_REQ_DELETE, filename);
            send(sockfd, request, strlen(request), 0);
            int n = recv(sockfd, response, sizeof(response) - 1, 0);
            if (n <= 0)
            {
                perror("recv");
                continue;
            }
            response[n] = '\0';
            if (!strncmp(response, "200", 3))
                printf("File deleted successfully.\n");
            else if (!strncmp(response, "403", 3))
                printf("Error: You are not the owner.\n");
            else if (!strncmp(response, "404", 3))
                printf("Error: File not found.\n");
            else
                printf("Unexpected response: %s\n", response);
        }

        // ----- LIST -----
        else if (strcmp(cmd, "LIST") == 0)
        {
            snprintf(request, sizeof(request), "%s\n", C_REQ_LIST);
            send(sockfd, request, strlen(request), 0);
            int n = recv(sockfd, response, sizeof(response) - 1, 0);
            if (n <= 0)
            {
                perror("recv");
                continue;
            }
            response[n] = '\0';
            char *payload = strchr(response, '\n');  // find first newline
            if (payload)
                printf("%s\n", payload + 1);         // print after the "200\n"
        }

        // ----- VIEW -----
        else if (strcmp(cmd, "VIEW") == 0)
        {
            char *flags = strtok(NULL, " ");
            if (!flags)
                flags = "-";
            snprintf(request, sizeof(request), "%s %s\n", C_REQ_VIEW, flags);
            send(sockfd, request, strlen(request), 0);
            int n = recv(sockfd, response, sizeof(response) - 1, 0);

            if (n <= 0)
            {
                perror("recv");
                continue;
            }
            response[n] = '\0';
            if (strncmp(response, "200", 3) == 0) {
                // Check if user asked for detailed view (-l)
                if (strstr(flags, "l") != NULL) {
                    printf("---------------------------------------------------------\n");
                    printf("|  Filename  | Words | Chars | Last Access Time | Owner |\n");
                    printf("|------------|-------|-------|------------------|-------|\n");
                    printf("%s", response + 4); // Print the rows sent by server (skip "200 ")
                    printf("---------------------------------------------------------\n");
                } else {
                    // Standard view
                    printf("=== Files Visible to You ===\n%s\n", response + 4);
                }
            } else {
                print_error(response);
            }
        }

        // ----- INFO -----
        else if (strcmp(cmd, "INFO") == 0)
        {
            char *filename = strtok(NULL, " ");
            if (!filename)
            {
                printf("Usage: INFO <filename>\n");
                continue;
            }
            snprintf(request, sizeof(request), "%s %s\n", C_REQ_INFO, filename);
            send(sockfd, request, strlen(request), 0);
            int n = recv(sockfd, response, sizeof(response) - 1, 0);
            if (n <= 0)
            {
                perror("recv");
                continue;
            }
            response[n] = '\0';
            if (!strncmp(response, "200", 3))
                printf("=== File Info ===\n%s\n", response + 4);
            else if (!strncmp(response, "403", 3))
                printf("Access denied.\n");
            else if (!strncmp(response, "404", 3))
                printf("File not found.\n");
            else
                printf("Unexpected response: %s\n", response);
        }

        // ----- WRITE -----
        else if (strcmp(cmd, "WRITE") == 0)
        {
            char *filename = strtok(NULL, " ");
            char *sentence_str = strtok(NULL, " ");
            if (!filename || !sentence_str)
            {
                printf("Usage: WRITE <filename> <sentence_num>\n");
                continue;
            }
            int sentence_num = atoi(sentence_str);

            // Step 1: Ask NM for SS info
            snprintf(request, sizeof(request), "%s %s\n", C_REQ_WRITE, filename);
            if (send(sockfd, request, strlen(request), 0) < 0)
            {
                perror("send");
                continue;
            }

            int n = recv(sockfd, response, sizeof(response) - 1, 0);
            if (n <= 0)
            {
                perror("recv");
                continue;
            }
            response[n] = '\0';

            if (strncmp(response, RESP_SS_INFO, 3) != 0)
            {
                // Check if it's our detailed 404 error
                if (strncmp(response, RESP_NOT_FOUND, 3) == 0) {
                    // Print the WHOLE message from the server (e.g., "404 Sentence index 5...")
                    // We skip the first 4 chars ("404 ")
                    printf("Error: %s", response + 4);
                } else {
                    // Otherwise, use the generic printer
                    print_error(response);
                }
                continue;
            }

            // Parse SS IP + port
            char ip[64], port_str[16];
            if (sscanf(response, "%*s %s %s", ip, port_str) != 2)
            {
                printf("Error: Invalid SS info.\n");
                continue;
            }
            int ss_port = atoi(port_str);

            // Step 2: Connect to SS
            int ss_sock = socket(AF_INET, SOCK_STREAM, 0);
            if (ss_sock < 0)
            {
                perror("socket");
                continue;
            }

            struct sockaddr_in ss_addr;
            memset(&ss_addr, 0, sizeof(ss_addr));
            ss_addr.sin_family = AF_INET;
            ss_addr.sin_port = htons(ss_port);
            inet_pton(AF_INET, ip, &ss_addr.sin_addr);

            if (connect(ss_sock, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) < 0)
            {
                perror("connect to SS");
                close(ss_sock);
                continue;
            }

            // Step 3: Request lock
            snprintf(request, sizeof(request), "%s %s %d\n", SS_LOCK, filename, sentence_num);
            send(ss_sock, request, strlen(request), 0);

            n = recv(ss_sock, response, sizeof(response) - 1, 0);
            if (n <= 0)
            {
                perror("recv");
                close(ss_sock);
                continue;
            }
            response[n] = '\0';

            if (!strncmp(response, RESP_LOCKED_ERR, 3))
            {
                printf("Error: Sentence is locked by another user.\n");
                close(ss_sock);
                continue;
            }
            else if (!strncmp(response, RESP_LOCKED, 3))
            {
                printf("Sentence locked. Enter updates (e.g., '3 new_word') or 'ETIRW' to finish:\n");
            }
            else
            {
                if (strncmp(response, RESP_NOT_FOUND, 3) == 0) {
                    // Print the WHOLE message from the server
                    // We skip the first 4 chars ("404 ") and the trailing newline
                    response[strcspn(response, "\n")] = 0; // Remove trailing newline
                    printf("Error: %s\n", response + 4); 
                } else {
                    // Otherwise, use the generic printer
                    print_error(response);
                }
                close(ss_sock);
                continue;
            }

            // Step 4: Interactive update loop
            char line[256];
            while (1)
            {
                printf("update> ");
                if (!fgets(line, sizeof(line), stdin))
                    break;
                if (strcmp(line, "ETIRW\n") == 0)
                {
                    snprintf(request, sizeof(request), "%s\n", SS_COMMIT);
                    send(ss_sock, request, strlen(request), 0);
                    break;
                }
                else
                {
                    int word_idx;
                    char new_word[128];
                    if (sscanf(line, "%d %[^\n]", &word_idx, new_word) != 2) {
                        printf("Usage: <word_index> <content>\n");
                        continue;
                    }
                    snprintf(request, sizeof(request), "%s %d %s\n", SS_UPDATE, word_idx, new_word);
                    send(ss_sock, request, strlen(request), 0);
                }
            }

            // Step 5: Wait for commit response
            n = recv(ss_sock, response, sizeof(response) - 1, 0);
            if (n <= 0)
            {
                perror("recv");
                close(ss_sock);
                continue;
            }
            response[n] = '\0';

            if (!strncmp(response, RESP_OK, 3))
                printf("Write successful.\n");
            else
                print_error(response);

            close(ss_sock);
        }

        // ----- UNDO -----
        else if (strcmp(cmd, "UNDO") == 0)
        {
            char *filename = strtok(NULL, " ");
            if (!filename)
            {
                printf("Usage: UNDO <filename>\n");
                continue;
            }
            snprintf(request, sizeof(request), "%s %s\n", C_REQ_UNDO, filename);
            send(sockfd, request, strlen(request), 0);

            int n = recv(sockfd, response, sizeof(response) - 1, 0);
            if (n <= 0)
            {
                perror("recv");
                continue;
            }
            response[n] = '\0';

            if (!strncmp(response, RESP_OK, 3))
                printf("Undo successful.\n");
            else
                print_error(response);
        }

        // ----- STREAM -----
        else if (strcmp(cmd, "STREAM") == 0)
        {
            char *filename = strtok(NULL, " ");
            if (!filename)
            {
                printf("Usage: STREAM <filename>\n");
                continue;
            }

            snprintf(request, sizeof(request), "%s %s\n", C_REQ_STREAM, filename);
            send(sockfd, request, strlen(request), 0);

            int n = recv(sockfd, response, sizeof(response) - 1, 0);
            if (n <= 0)
            {
                perror("recv");
                continue;
            }
            response[n] = '\0';

            if (strncmp(response, RESP_SS_INFO, 3) != 0)
            {
                print_error(response);
                continue;
            }

            char ip[64], port_str[16];
            sscanf(response, "%*s %s %s", ip, port_str);
            int ss_port = atoi(port_str);

            int ss_sock = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in ss_addr;
            memset(&ss_addr, 0, sizeof(ss_addr));
            ss_addr.sin_family = AF_INET;
            ss_addr.sin_port = htons(ss_port);
            inet_pton(AF_INET, ip, &ss_addr.sin_addr);

            if (connect(ss_sock, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) < 0)
            {
                perror("connect SS");
                close(ss_sock);
                continue;
            }

            snprintf(request, sizeof(request), "%s %s\n", SS_GET_STREAM, filename);
            send(ss_sock, request, strlen(request), 0);

            // Set a receive timeout to detect if server hangs or dies
            struct timeval timeout;
            timeout.tv_sec = 5;  // 5 second timeout
            timeout.tv_usec = 0;
            setsockopt(ss_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

            printf("Streaming %s...\n", filename);
            bool had_error = false;
            bool received_any_data = false;
            int idle_count = 0;
            
            while (1)
            {
                n = recv(ss_sock, response, sizeof(response) - 1, 0);
                
                if (n < 0)
                {
                    // Check if it's a timeout or actual error
                    if (errno == EWOULDBLOCK || errno == EAGAIN)
                    {
                        // Timeout - server might be dead or slow
                        idle_count++;
                        if (idle_count > 1 && received_any_data)
                        {
                            // If we got data before and now timeout, server likely died
                            had_error = true;
                            break;
                        }
                        continue;
                    }
                    else
                    {
                        // Actual error (connection reset, etc.)
                        had_error = true;
                        break;
                    }
                }
                else if (n == 0)
                {
                    // Connection closed by server
                    // This is only normal if we received some data (could be empty file)
                    // But if we were actively receiving and it suddenly closes, it's suspicious
                    break;
                }
                else
                {
                    // Successfully received data
                    received_any_data = true;
                    idle_count = 0;  // Reset idle counter
                    response[n] = '\0';
                    printf("%s", response);
                    fflush(stdout);
                }
            }
            
            if (had_error)
            {
                printf("\n--- Error: Storage server disconnected during streaming ---\n");
            }
            else
            {
                printf("\n--- End of stream ---\n");
            }
            close(ss_sock);
        }

// ----- EXEC -----
        else if (strcmp(cmd, "EXEC") == 0)
        {
            char *filename = strtok(NULL, " ");
            if (!filename)
            {
                printf("Usage: EXEC <filename>\n");
                continue;
            }

            snprintf(request, sizeof(request), "%s %s\n", C_REQ_EXEC, filename);
            send(sockfd, request, strlen(request), 0);

            // Do the FIRST read
            n = recv(sockfd, response, sizeof(response) - 1, 0);
            if (n <= 0)
            {
                perror("recv");
                continue;
            }
            response[n] = '\0';

            // Check for 200 OK
            if (strncmp(response, RESP_OK, 3) != 0)
            {
                print_error(response); // Handle errors like 404, 403
            }
            else
            {
                printf("Executing script remotely... Success:\n");

                // --- START NEW ROBUST LOGIC ---
                char* payload_start = response + 4; // Skip "200\n"
                char* end_marker = NULL;
                bool done = false;

                // Check if the FIRST buffer already contains the end marker
                end_marker = strstr(payload_start, RESP_EXEC_DONE);
                if (end_marker) {
                    *end_marker = '\0'; // Truncate the buffer before the marker
                    done = true;
                }
                
                // Print whatever payload was in the first buffer
                if (strlen(payload_start) > 0) {
                    printf("%s", payload_start);
                    fflush(stdout);
                }

                // If we didn't find the end marker, loop for the rest
                if (!done) {
                    while ((n = recv(sockfd, response, sizeof(response) - 1, 0)) > 0)
                    {
                        response[n] = '\0';
                        
                        // Check if this buffer has the end marker
                        end_marker = strstr(response, RESP_EXEC_DONE);
                        if (end_marker) {
                            *end_marker = '\0'; // Truncate
                            done = true;
                        }

                        // Print the (possibly truncated) buffer
                        if (strlen(response) > 0) {
                            printf("%s", response);
                            fflush(stdout);
                        }

                        if (done) {
                            break; // Exit the loop
                        }
                    }
                }
                printf("\n--- End of execution output ---\n");
                // --- END NEW ROBUST LOGIC ---
            }
        }
        
        // ----- CHECKPOINT -----
        else if (strcmp(cmd, "CHECKPOINT") == 0)
        {
            char *filename = strtok(NULL, " ");
            char *tag = strtok(NULL, " ");
            if (!filename || !tag)
            {
                printf("Usage: CHECKPOINT <filename> <tag>\n");
                continue;
            }
            
            snprintf(request, sizeof(request), "%s %s %s\n", C_REQ_CHECKPOINT, filename, tag);
            send(sockfd, request, strlen(request), 0);
            
            int n = recv(sockfd, response, sizeof(response) - 1, 0);
            if (n <= 0)
            {
                perror("recv");
                continue;
            }
            response[n] = '\0';
            
            if (!strncmp(response, "200", 3))
                printf("Checkpoint '%s' created successfully for '%s'.\n", tag, filename);
            else if (!strncmp(response, "403", 3))
                printf("Error: Only the file owner can create checkpoints.\n");
            else if (!strncmp(response, "404", 3))
                printf("Error: File not found.\n");
            else
                print_error(response);
        }

        // ----- VIEWCHECKPOINT -----
        else if (strcmp(cmd, "VIEWCHECKPOINT") == 0)
        {
            char *filename = strtok(NULL, " ");
            char *tag = strtok(NULL, " ");
            if (!filename || !tag)
            {
                printf("Usage: VIEWCHECKPOINT <filename> <tag>\n");
                continue;
            }
            
            snprintf(request, sizeof(request), "%s %s %s\n", C_REQ_VIEWCHECKPOINT, filename, tag);
            send(sockfd, request, strlen(request), 0);
            
            int n = recv(sockfd, response, sizeof(response) - 1, 0);
            if (n <= 0)
            {
                perror("recv");
                continue;
            }
            response[n] = '\0';
            
            if (!strncmp(response, "200", 3))
            {
                printf("===== Checkpoint '%s' for %s =====\n", tag, filename);
                
                // Use robust marker-based detection (like EXEC command)
                char* payload_start = response + 4; // Skip "200\n"
                char* end_marker = NULL;
                bool done = false;

                end_marker = strstr(payload_start, RESP_EXEC_DONE);
                if (end_marker) {
                    *end_marker = '\0';
                    done = true;
                }
                if (strlen(payload_start) > 0) {
                    printf("%s", payload_start);
                    fflush(stdout);
                }

                if (!done) {
                    while ((n = recv(sockfd, response, sizeof(response) - 1, 0)) > 0)
                    {
                        response[n] = '\0';
                        end_marker = strstr(response, RESP_EXEC_DONE);
                        if (end_marker) {
                            *end_marker = '\0';
                            done = true;
                        }
                        if (strlen(response) > 0) {
                            printf("%s", response);
                            fflush(stdout);
                        }
                        if (done) {
                            break;
                        }
                    }
                }
                printf("\n===== End of checkpoint =====\n");
            }
            else if (!strncmp(response, "403", 3))
                printf("Error: Access denied.\n");
            else if (!strncmp(response, "404", 3))
                printf("Error: Checkpoint not found.\n");
            else
                print_error(response);
        }

        // ----- REVERT -----
        else if (strcmp(cmd, "REVERT") == 0)
        {
            char *filename = strtok(NULL, " ");
            char *tag = strtok(NULL, " ");
            if (!filename || !tag)
            {
                printf("Usage: REVERT <filename> <tag>\n");
                continue;
            }
            
            // Confirmation prompt
            printf("Are you sure you want to revert '%s' to checkpoint '%s'? (yes/no): ", 
                   filename, tag);
            char confirm[10];
            if (fgets(confirm, sizeof(confirm), stdin) == NULL) {
                printf("Input error.\n");
                continue;
            }
            
            // Remove newline
            size_t clen = strlen(confirm);
            if (clen > 0 && confirm[clen - 1] == '\n') {
                confirm[clen - 1] = '\0';
            }
            
            if (strcmp(confirm, "yes") != 0) {
                printf("Revert cancelled.\n");
                continue;
            }
            
            snprintf(request, sizeof(request), "%s %s %s\n", C_REQ_REVERT, filename, tag);
            send(sockfd, request, strlen(request), 0);
            
            int n = recv(sockfd, response, sizeof(response) - 1, 0);
            if (n <= 0)
            {
                perror("recv");
                continue;
            }
            response[n] = '\0';
            
            if (!strncmp(response, "200", 3))
                printf("File '%s' successfully reverted to checkpoint '%s'.\n", filename, tag);
            else if (!strncmp(response, "403", 3))
                printf("Error: You don't have write access to this file.\n");
            else if (!strncmp(response, "404", 3))
                printf("Error: Checkpoint not found.\n");
            else if (!strncmp(response, "504", 3))
                printf("Error: Cannot revert while file is locked by another user.\n");
            else
                print_error(response);
        }

        // ----- LISTCHECKPOINTS -----
        else if (strcmp(cmd, "LISTCHECKPOINTS") == 0)
        {
            char *filename = strtok(NULL, " ");
            if (!filename)
            {
                printf("Usage: LISTCHECKPOINTS <filename>\n");
                continue;
            }
            
            snprintf(request, sizeof(request), "%s %s\n", C_REQ_LISTCHECKPOINTS, filename);
            send(sockfd, request, strlen(request), 0);
            
            int n = recv(sockfd, response, sizeof(response) - 1, 0);
            if (n <= 0)
            {
                perror("recv");
                continue;
            }
            response[n] = '\0';
            
            if (!strncmp(response, "200", 3))
            {
                printf("===== Checkpoints for %s =====\n", filename);
                printf("Tag                | Created At\n");
                printf("-------------------+--------------------\n");
                
                // Use robust marker-based detection (like EXEC command)
                char* payload_start = response + 4; // Skip "200\n"
                char* end_marker = NULL;
                bool done = false;

                end_marker = strstr(payload_start, RESP_EXEC_DONE);
                if (end_marker) {
                    *end_marker = '\0';
                    done = true;
                }
                if (strlen(payload_start) > 0) {
                    printf("%s", payload_start);
                    fflush(stdout);
                }

                if (!done) {
                    while ((n = recv(sockfd, response, sizeof(response) - 1, 0)) > 0)
                    {
                        response[n] = '\0';
                        end_marker = strstr(response, RESP_EXEC_DONE);
                        if (end_marker) {
                            *end_marker = '\0';
                            done = true;
                        }
                        if (strlen(response) > 0) {
                            printf("%s", response);
                            fflush(stdout);
                        }
                        if (done) {
                            break;
                        }
                    }
                }
                printf("================================\n");
            }
            else if (!strncmp(response, "403", 3))
                printf("Error: Access denied.\n");
            else if (!strncmp(response, "404", 3))
                printf("Error: File not found.\n");
            else
                print_error(response);
        }
        
        // ----- REQACCESS (Request access to a file) -----
        else if (strcmp(cmd, "REQACCESS") == 0)
        {
            char *filename = strtok(NULL, " ");
            char *perm = strtok(NULL, " ");
            if (!filename || !perm)
            {
                printf("Usage: REQACCESS <filename> <-R|-W>\n");
                continue;
            }
            snprintf(request, sizeof(request), "%s %s %s\n",
                     C_REQ_REQUEST_ACC, filename, perm);
            send(sockfd, request, strlen(request), 0);
            int n = recv(sockfd, response, sizeof(response) - 1, 0);
            if (n <= 0)
            {
                perror("recv");
                continue;
            }
            response[n] = '\0';
            if (!strncmp(response, "200", 3))
            {
                printf("%s", response + 4); // Skip "200 "
            }
            else
            {
                printf("%s", response + 4); // Error message
            }
        }
        
        // ----- VIEWREQUESTS (View pending requests for your file) -----
        else if (strcmp(cmd, "VIEWREQUESTS") == 0)
        {
            char *filename = strtok(NULL, " ");
            if (!filename)
            {
                printf("Usage: VIEWREQUESTS <filename>\n");
                continue;
            }
            snprintf(request, sizeof(request), "%s %s\n",
                     C_REQ_VIEW_REQUESTS, filename);
            send(sockfd, request, strlen(request), 0);
            int n = recv(sockfd, response, sizeof(response) - 1, 0);
            if (n <= 0)
            {
                perror("recv");
                continue;
            }
            response[n] = '\0';
            if (!strncmp(response, "200", 3))
            {
                printf("%s", response + 4); // Skip "200\n"
            }
            else
            {
                print_error(response);
            }
        }
        
        // ----- APPROVE (Approve an access request) -----
        else if (strcmp(cmd, "APPROVE") == 0)
        {
            char *req_id = strtok(NULL, " ");
            if (!req_id)
            {
                printf("Usage: APPROVE <request_id>\n");
                continue;
            }
            snprintf(request, sizeof(request), "%s %s\n",
                     C_REQ_APPROVE_ACC, req_id);
            send(sockfd, request, strlen(request), 0);
            int n = recv(sockfd, response, sizeof(response) - 1, 0);
            if (n <= 0)
            {
                perror("recv");
                continue;
            }
            response[n] = '\0';
            if (!strncmp(response, "200", 3))
            {
                printf("%s", response + 4); // Skip "200 "
            }
            else
            {
                printf("%s", response + 4); // Error message
            }
        }
        
        // ----- DENY (Deny an access request) -----
        else if (strcmp(cmd, "DENY") == 0)
        {
            char *req_id = strtok(NULL, " ");
            if (!req_id)
            {
                printf("Usage: DENY <request_id>\n");
                continue;
            }
            snprintf(request, sizeof(request), "%s %s\n",
                     C_REQ_DENY_ACC, req_id);
            send(sockfd, request, strlen(request), 0);
            int n = recv(sockfd, response, sizeof(response) - 1, 0);
            if (n <= 0)
            {
                perror("recv");
                continue;
            }
            response[n] = '\0';
            if (!strncmp(response, "200", 3))
            {
                printf("%s", response + 4); // Skip "200 "
            }
            else
            {
                printf("%s", response + 4); // Error message
            }
        }
        
        // ----- MYREQUESTS (View your own access requests) -----
        else if (strcmp(cmd, "MYREQUESTS") == 0)
        {
            snprintf(request, sizeof(request), "%s\n", C_REQ_MY_REQUESTS);
            send(sockfd, request, strlen(request), 0);
            int n = recv(sockfd, response, sizeof(response) - 1, 0);
            if (n <= 0)
            {
                perror("recv");
                continue;
            }
            response[n] = '\0';
            printf("%s", response + 4); // Skip "200\n"
        }
        
        // ----- Folder commands -----
        else if (strcmp(cmd, "CREATEFOLDER") == 0) {
            char *folder = strtok(NULL, " ");
            if (!folder) { printf("Usage: CREATEFOLDER <name>\n"); continue; }
            snprintf(request, sizeof(request), "%s %s\n", C_REQ_CREATEFOLDER, folder);
            send(sockfd, request, strlen(request), 0);
            
            int n = recv(sockfd, response, sizeof(response)-1, 0);
            response[n] = 0;
            if(strncmp(response, "200", 3) == 0) printf("%s", response+4);
            else print_error(response);
        }
        else if (strcmp(cmd, "MOVE") == 0) {
            char *file = strtok(NULL, " ");
            char *folder = strtok(NULL, " ");
            if (!file || !folder) { printf("Usage: MOVE <file> <folder>\n"); continue; }
            snprintf(request, sizeof(request), "%s %s %s\n", C_REQ_MOVE, file, folder);
            send(sockfd, request, strlen(request), 0);
            
            int n = recv(sockfd, response, sizeof(response)-1, 0);
            response[n] = 0;
            if(strncmp(response, "200", 3) == 0) printf("%s", response+4);
            else print_error(response);
        }
        else if (strcmp(cmd, "VIEWFOLDER") == 0) {
            char *folder = strtok(NULL, " ");
            if (!folder) { printf("Usage: VIEWFOLDER <name>\n"); continue; }
            snprintf(request, sizeof(request), "%s %s\n", C_REQ_VIEWFOLDER, folder);
            send(sockfd, request, strlen(request), 0);
            
            // This response might be multi-line, but isn't streaming.
            // Single recv is fine for MVP.
            int n = recv(sockfd, response, sizeof(response)-1, 0);
            response[n] = 0;
            if(strncmp(response, "200", 3) == 0) printf("%s", response+4);
            else print_error(response);
        }
        else if (strcmp(cmd, "HELP") == 0)
        {
            printf("\n--- Available Commands ---\n");
            printf("File Operations:\n");
            printf("  CREATE <filename>              : Create a new file\n");
            printf("  READ <filename>                : Read file content\n");
            printf("  WRITE <filename> <sentence_id> : Edit a specific sentence (ends with ETIRW)\n");
            printf("  DELETE <filename>              : Delete a file\n");
            printf("  INFO <filename>                : View file metadata (owner, size, etc.)\n");
            printf("  STREAM <filename>              : Stream content word-by-word\n");
            printf("  EXEC <filename>                : Execute file content as a script\n");
             
            printf("\nAccess Control:\n"); 
            printf("  ADDACCESS -R <file> <user>     : Grant Read access\n");
            printf("  ADDACCESS -W <file> <user>     : Grant Write access\n");
            printf("  REMACCESS <file> <user>        : Revoke access\n");
            printf("  REQACCESS <file> -R/-W         : Request access from owner\n");
            printf("  VIEWREQUESTS <file>            : View pending requests (Owner only)\n");
            printf("  APPROVE <req_id>               : Approve a request\n");
            printf("  DENY <req_id>                  : Deny a request\n");
            printf("  MYREQUESTS                     : View status of your sent requests\n");
 
            printf("\nCheckpoints & Folders:\n"); 
            printf("  CHECKPOINT <file> <tag>        : Save current state as a checkpoint\n");
            printf("  VIEWCHECKPOINT <file> <tag>    : Read a specific checkpoint\n");
            printf("  REVERT <file> <tag>            : Revert file to a checkpoint\n");
            printf("  LISTCHECKPOINTS <file>         : List all checkpoints for a file\n");
            printf("  CREATEFOLDER <name>            : Create a new folder\n");
            printf("  MOVE <file> <folder>           : Move a file into a folder\n");
            printf("  VIEWFOLDER <name>              : List folder contents\n");
 
            printf("\nGeneral:\n"); 
            printf("  VIEW [-l, -a]                  : List accessible files (-l for details and -a for admin view of all files)\n");
            printf("  LIST                           : List all registered users\n");
            printf("  EXIT / QUIT                    : Disconnect and exit\n");
            printf("--------------------------\n");
        }
        // ----- Exit command -----
        else if (strcmp(cmd, "QUIT") == 0 || strcmp(cmd, "EXIT") == 0)
        {
            printf("Exiting client...\n");
            break;
        }
        // ----- Other commands -----
        else
        {
            printf("Unsupported command (for now). Try CREATE <filename>, READ <filename>, or EXIT.\n");
        }
    }

    // Clean up socket and exit
    close(sockfd);
    return 0;
}
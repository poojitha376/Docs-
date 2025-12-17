#ifndef PROTOCOL_H
#define PROTOCOL_H

/*
 * ============================================================================
 * protocol.h
 *
 * This header file defines the complete network protocol for the
 * Distributed File System project.
 *
 * It must be included by:
 * 1. client
 * 2. name_server
 * 3. storage_server
 *
 * It contains all shared constants, command strings, and response codes.
 * ============================================================================
 */


/*
 * ============================================================================
 * SECTION 1: CORE NETWORK & BUFFER CONSTANTS
 * ============================================================================
 */

/* The single "well-known" public port the Name Server listens on. */
#define NM_LISTEN_PORT 9001

/* Standard buffer size for sending/receiving command strings. */
#define MAX_MSG_LEN 1024

/* Standard max length for file paths, usernames, etc. */
#define MAX_PATH_LEN 256
#define MAX_USERNAME_LEN 64

/* Maximum number of Storage Servers that can connect to the system */
#define MAX_SS 20


/*
 * ============================================================================
 * SECTION 2: COMMANDS (CLIENT -> NAME SERVER)
 * ============================================================================
 */

/* [Phase 1] Initial registration: "C_INIT <username>" */
#define C_INIT "C_INIT"

/* [Phase 1] Create file: "C_CREATE <filename>" */
#define C_REQ_CREATE "C_CREATE"

/* [Phase 1 & 3] Requests for file ops (NM replies with SS info) */
#define C_REQ_READ   "C_READ"   /* "C_READ <filename>" */
#define C_REQ_WRITE  "C_WRITE"  /* "C_WRITE <filename>" */
#define C_REQ_STREAM "C_STREAM" /* "C_STREAM <filename>" */

/* [Phase 2] Get file/user metadata (NM handles directly) */
#define C_REQ_VIEW "C_VIEW" /* "C_VIEW <flags>" (e.g., "C_VIEW -al") */
#define C_REQ_INFO "C_INFO" /* "C_INFO <filename>" */
#define C_REQ_LIST "C_LIST" /* "C_LIST" (Lists all registered users) */

/* [Hierarchical] Folder operations */
#define C_REQ_CREATEFOLDER "C_CREATEFOLDER" /* "C_CREATEFOLDER <foldername>" */
#define C_REQ_MOVE         "C_MOVE"         /* "C_MOVE <filename> <dest_folder>" */
#define C_REQ_VIEWFOLDER   "C_VIEWFOLDER"   /* "C_VIEWFOLDER <foldername>" */

/* [Phase 2] Delete file */
#define C_REQ_DELETE "C_DELETE" /* "C_DELETE <filename>" */

/* [Phase 2] Access control */
#define C_REQ_ADD_ACC "C_ADD_ACC" /* "C_ADD_ACC <filename> <username> <perm_flag>" */
#define C_REQ_REM_ACC "C_REM_ACC" /* "C_REM_ACC <filename> <username>" */

/* [Phase 2+] Access request system */
#define C_REQ_REQUEST_ACC "C_REQ_ACC"     /* "C_REQ_ACC <filename> <perm_flag>" */
#define C_REQ_VIEW_REQUESTS "C_VIEW_REQ"  /* "C_VIEW_REQ <filename>" */
#define C_REQ_APPROVE_ACC "C_APPROVE"     /* "C_APPROVE <request_id>" */
#define C_REQ_DENY_ACC "C_DENY"           /* "C_DENY <request_id>" */
#define C_REQ_MY_REQUESTS "C_MY_REQ"      /* "C_MY_REQ" */

/* [Phase 3] Undo command */
#define C_REQ_UNDO "C_UNDO" /* "C_UNDO <filename>" */

/* [Phase 3] Execute command */
#define C_REQ_EXEC "C_EXEC" /* "C_EXEC <filename>" */

/* [Phase 3+] Checkpoint commands */
#define C_REQ_CHECKPOINT "C_CHECKPOINT"       /* "C_CHECKPOINT <filename> <tag>" */
#define C_REQ_VIEWCHECKPOINT "C_VIEWCHECKPOINT" /* "C_VIEWCHECKPOINT <filename> <tag>" */
#define C_REQ_REVERT "C_REVERT"               /* "C_REVERT <filename> <tag>" */
#define C_REQ_LISTCHECKPOINTS "C_LISTCHECKPOINTS" /* "C_LISTCHECKPOINTS <filename>" */

/* [Phase 3] After a successful write, SS tells NM new metadata */
/* "S_META_UPDATE <filename> <word_count> <char_count>" */
#define S_META_UPDATE "S_META_UPDATE"

/*
 * ============================================================================
 * SECTION 3: COMMANDS (STORAGE SERVER -> NAME SERVER)
 * ============================================================================
 */

/* [Phase 1] Initial registration */
/* "S_INIT <ip_addr> <nm_facing_port> <client_facing_port>" */
#define S_INIT "S_INIT"

/* [Phase 3] After a successful write, SS tells NM new metadata */
/* "S_META_UPDATE <filename> <word_count> <char_count>" */
#define S_META_UPDATE "S_META_UPDATE"


/*
 * ============================================================================
 * SECTION 4: COMMANDS (NAME SERVER -> STORAGE SERVER)
 * ============================================================================
 */

/* [Phase 1] Tell SS to create a new, empty file */
#define NM_CREATE "NM_CREATE" /* "NM_CREATE <filename>" */

/* [Phase 2] Tell SS to delete a file */
#define NM_DELETE "NM_DELETE" /* "NM_DELETE <filename>" */

/* [Hierarchical] Tell SS to rename/move a file */
#define NM_RENAME "NM_RENAME" /* "NM_RENAME <old_path> <new_path>" */

/* [Phase 3] Tell SS to revert the last change */
#define NM_UNDO "NM_UNDO" /* "NM_UNDO <filename>" */

/* [Phase 3+] Checkpoint commands (NM -> SS) */
#define NM_CHECKPOINT "NM_CHECKPOINT"         /* "NM_CHECKPOINT <filename> <tag>" */
#define NM_VIEWCHECKPOINT "NM_VIEWCHECKPOINT" /* "NM_VIEWCHECKPOINT <filename> <tag>" */
#define NM_REVERT "NM_REVERT"                 /* "NM_REVERT <filename> <tag>" */
#define NM_LISTCHECKPOINTS "NM_LISTCHECKPOINTS" /* "NM_LISTCHECKPOINTS <filename>" */

/* [Phase 3] NM needs a file's content (for EXEC) */
#define NM_GET_FILE "NM_GET_FILE" /* "NM_GET_FILE <filename>" */


/*
 * ============================================================================
 * SECTION 5: COMMANDS (CLIENT -> STORAGE SERVER)
 * (Used on the direct C-SS connection)
 * ============================================================================
 */

/* [Phase 1] Request file contents for READ */
#define SS_GET_FILE "SS_GET_FILE" /* "SS_GET_FILE <filename>" */

/* [Phase 3] Request file contents for STREAM */
#define SS_GET_STREAM "SS_GET_STREAM" /* "SS_GET_STREAM <filename>" */

/* [Phase 3] The multi-step WRITE protocol */
/* 1. Lock: "SS_LOCK <filename> <sentence_number>" */
#define SS_LOCK "SS_LOCK"
/* 2. Update: "SS_UPDATE <word_index> <content>" */
#define SS_UPDATE "SS_UPDATE"
/* 3. Commit: "SS_COMMIT" (the ETIRW command) */
#define SS_COMMIT "SS_COMMIT"

/* [Phase 3+] Checkpoint commands (for NM-as-client) */
#define SS_VIEWCHECKPOINT "SS_VIEWCHECKPOINT"
#define SS_LISTCHECKPOINTS "SS_LISTCHECKPOINTS"


/*
 * ============================================================================
 * SECTION 6: UNIVERSAL RESPONSE PREFIXES (STATUS CODES)
 * ============================================================================
 */

/* --- Success (2xx) --- */

/* "200" (Generic success) */
#define RESP_OK "200"
/* "201" (SS to Client on successful lock) */
#define RESP_LOCKED "201"
/* "202 <ip> <port>" (NM to Client with SS info) */
#define RESP_SS_INFO "202"

/* --- Client Errors (4xx) --- */

/* "400" (Malformed command, bad args) */
#define RESP_BAD_REQ "400"
/* "403" (No permission, not owner, etc.) */
#define RESP_FORBIDDEN "403"
/* "404" (File, user, or sentence not found) */
#define RESP_NOT_FOUND "404"
/* "409" (File already exists on CREATE) */
#define RESP_CONFLICT "409"

/* --- Server Errors (5xx) --- */

/* "500" (Generic server-side crash) */
#define RESP_SRV_ERR "500"
/* "503" (NM can't reach SS for a task) */
#define RESP_SS_DOWN "503"
/* "504" (WRITE failed, sentence locked by another user) */
#define RESP_LOCKED_ERR "504"
/* "299" (NM to Client, signals end of EXEC output) */
#define RESP_EXEC_DONE "299 EXEC_DONE"

/*
 * ============================================================================
 * SECTION 7: PROTOCOL ARGUMENT CONSTANTS
 * ============================================================================
 */

/* [Phase 2] For ADDACCESS command */
#define PERM_READ 'R'
#define PERM_WRITE 'W'

/* [Phase 2] For VIEW command */
#define FLAG_ALL 'a'
#define FLAG_LONG 'l'

/* [Phase 3] For STREAM command */
/* Delay in microseconds (0.1 seconds = 100,000 us) */
#define STREAM_DELAY_US 100000

/* [Phase 3+] For CHECKPOINT commands */
#define MAX_CHECKPOINT_TAG 64
#define MAX_CHECKPOINTS_PER_FILE 20

/* [Phase 2+] Access Request Status */
#define REQ_STATUS_PENDING 'P'
#define REQ_STATUS_APPROVED 'A'
#define REQ_STATUS_DENIED 'D'

#define NM_LOG_FILE "nm.log"
#define SS_LOG_FILE "ss.log"

/*
 * ============================================================================
 * SECTION 8: FAULT TOLERANCE & REPLICATION CONSTANTS
 * ============================================================================
 */

/* Number of replicas for each file (primary + 2 replicas = 3 total copies) */
#define REPLICATION_FACTOR 3

/* Heartbeat interval in seconds (NM pings SS every 5 seconds) */
#define HEARTBEAT_INTERVAL 5

/* Timeout to declare SS as failed (if no response for 15 seconds) */
#define SS_TIMEOUT 15

/* Maximum number of pending async writes per SS */
#define MAX_PENDING_WRITES 100

/*
 * ============================================================================
 * SECTION 9: REPLICATION PROTOCOL COMMANDS
 * ============================================================================
 */

/* [Replication] NM requests SS to replicate a file from another SS */
/* "NM_REPLICATE <filename> <source_ss_ip> <source_ss_port>" */
#define NM_REPLICATE "NM_REPLICATE"

/* [Replication] NM sends heartbeat to check if SS is alive */
/* "NM_HEARTBEAT" */
#define NM_HEARTBEAT "NM_HEARTBEAT"

/* [Replication] SS responds to heartbeat */
/* "S_HEARTBEAT_ACK" */
#define S_HEARTBEAT_ACK "S_HEARTBEAT_ACK"

/* [Replication] NM requests full file list from SS for recovery */
/* "NM_LIST_FILES" */
#define NM_LIST_FILES "NM_LIST_FILES"

/* [Replication] SS responds with file list (multi-line response) */
/* "S_FILE_LIST\n<filename1>\n<filename2>\n...\nEND_LIST" */
#define S_FILE_LIST "S_FILE_LIST"
#define S_FILE_LIST_END "END_LIST"

/* [Replication] NM tells SS to sync a file from another SS during recovery */
/* "NM_SYNC <filename> <source_ss_ip> <source_ss_port>" */
#define NM_SYNC "NM_SYNC"

/* [Replication] SS-to-SS: Request file for replication */
/* "SS_REQ_REPLICA <filename>" */
#define SS_REQ_REPLICA "SS_REQ_REPLICA"

// Add after #define SS_LOG_FILE "ss.log"

/* ============================================================================
 * NETWORK CONFIGURATION
 * ============================================================================
 */

// Default IPs/Hosts - can be overridden at compile time or runtime
#ifndef NM_HOST
#define NM_HOST "127.0.0.1"     // Default to localhost, override for different networks
#endif

#ifndef NM_PORT
#define NM_PORT 9001            // This stays hardcoded as "well-known port"
#endif

#endif // PROTOCOL_H
[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/0ek2UV58)

## We have done all bonus parts

## Project Structure

```text
course-project-codeshark/
├── bin/                  # Compiled executables
├── name_server/          # NM Source (Metadata, Trie, Load Balancer)
├── storage_server/       # SS Source (Persistence, Locking, Sync)
├── client/               # Client Source (CLI, Network Interface)
├── include/              # Header files (protocol.h, config.h)
├── ss_data/              # Data storage directory (created at runtime)
└── Makefile              # Build automation
```

-----

## Installation & Build

### Compilation

To build the Name Server, Storage Server, and Client:

```bash
# Clean previous builds
make clean

# Compile all binaries
make
```

This creates three executables in the `bin/` directory.

-----

## Running the System

You can run Docs++ on a single laptop (Localhost) or across a network (Multi-Machine).

### Scenario A: Localhost (Single Machine Testing)

Open 3 separate terminals.

1.  **Start Name Server:**
    ```bash
    ./bin/name_server
    ```
2.  **Start Storage Server:**
    ```bash
    # Usage: ./bin/storage_server <Port> <NM_IP> <SS_IP>
    ./bin/storage_server 9002 127.0.0.1 127.0.0.1
    ```
3.  **Start Client:**
    ```bash
    # Usage: ./bin/client <NM_IP>
    ./bin/client 127.0.0.1
    ```

### Scenario B: Multi-Machine (LAN / Wi-Fi)

*Assume **Machine A** (IP: `192.168.1.50`) hosts the Name Server, and **Machine B** (IP: `192.168.1.60`) hosts a Client/SS.*

1.  **Machine A (Name Server):**
    ```bash
    ./bin/name_server
    ```
2.  **Machine A (Storage Server 1):**
    ```bash
    ./bin/storage_server 9002 192.168.1.50 192.168.1.50
    ```
3.  **Machine B (Client):**
    ```bash
    ./bin/client 192.168.1.50
    ```

> **Firewall Warning:** Ensure ports **9001** (NM) and **9002** (SS) are allowed through your firewall (`sudo ufw allow 9001/tcp`).

-----

## Command Reference

### 1\. File Operations

| Command | Syntax | Description |
| :--- | :--- | :--- |
| **CREATE** | `CREATE <filename>` | Create a new empty file. |
| **READ** | `READ <filename>` | Read file content (downloads from SS). |
| **WRITE** | `WRITE <filename> <sentence_id>` | Transactional write. Locks specific sentence ID. Type updates, end with `ETIRW`. |
| **DELETE** | `DELETE <filename>` | Delete a file permanently. |
| **INFO** | `INFO <filename>` | View file size, owner, and created/modified timestamps. |
| **STREAM** | `STREAM <filename>` | Stream file content word-by-word (simulates audio). |
| **EXEC** | `EXEC <filename>` | Execute a script file remotely on the SS and get output. |

### 2\. Access Control (ACL)

| Command | Syntax | Description |
| :--- | :--- | :--- |
| **ADDACCESS** | `ADDACCESS -R <file> <user>` | Grant **Read** access to a user. |
| | `ADDACCESS -W <file> <user>` | Grant **Write** (and Read) access to a user. |
| **REMACCESS** | `REMACCESS <file> <user>` | Revoke all permissions for a user. |
| **REQACCESS** | `REQACCESS <file> -R` | Request Read permission from the owner. |
| | `REQACCESS <file> -W` | Request Write permission from the owner. |
| **VIEWREQUESTS**| `VIEWREQUESTS <filename>` | (Owner only) View pending access requests. |
| **APPROVE** | `APPROVE <req_id>` | Approve a pending request ID. |
| **DENY** | `DENY <req_id>` | Deny a pending request ID. |
| **MYREQUESTS** | `MYREQUESTS` | View status of your own sent requests. |

### 3\. Versioning & Organization

| Command | Syntax | Description |
| :--- | :--- | :--- |
| **CHECKPOINT** | `CHECKPOINT <file> <tag>` | Save current file state as a checkpoint. |
| **VIEWCHECKPOINT**| `VIEWCHECKPOINT <file> <tag>` | Read the content of a saved checkpoint. |
| **REVERT** | `REVERT <file> <tag>` | Revert the live file to a previous checkpoint. |
| **LISTCHECKPOINTS**| `LISTCHECKPOINTS <file>` | List all tags and timestamps for a file. |
| **CREATEFOLDER**| `CREATEFOLDER <name>` | Create a logical folder. |
| **MOVE** | `MOVE <file> <folder>` | Move a file into a folder. |
| **VIEWFOLDER** | `VIEWFOLDER <name>` | List contents of a folder. |

### 4\. General

| Command | Syntax | Description |
| :--- | :--- | :--- |
| **VIEW** | `VIEW [-l, -a]` | List files. `-l` provides detailed table view.  `-a` provides admin view of all files. |
| **LIST** | `LIST` | List all registered users. |
| **EXIT** | `EXIT` | Disconnect the client. |
| **QUIT** | `QUIT` | Disconnect the client. |

### 5\. Unique Feature

| Command | Syntax | Description |
| :--- | :--- | :--- |
| **HELP** | `HELP` | Prints the syntax of all supported commands. |

### Whenever a user types out a command with wrong syntax, we print the correct syntax as well. Example given below: 
```text
(base) jkb@jkb-HP-ProBook-450-15-6-inch-G10-Notebook-PC:~/Documents/sem3/osn/finalproject/course-project-codeshark$ ./bin/client 127.0.0.1
Enter username: alice
Registered with Name Server as 'alice'.
> ADDACCESS
Usage: ADDACCESS <-R|-W> <filename> <username>

> HELP

--- Available Commands ---
File Operations:
  CREATE <filename>             : Create a new file
  READ <filename>               : Read file content
  WRITE <filename> <sentence_id>: Edit a specific sentence (ends with ETIRW)
  DELETE <filename>             : Delete a file
  INFO <filename>               : View file metadata (owner, size, etc.)
  STREAM <filename>             : Stream content word-by-word
  EXEC <filename>               : Execute file content as a script

Access Control:
  ADDACCESS -R <file> <user>    : Grant Read access
  ADDACCESS -W <file> <user>    : Grant Write access
  REMACCESS <file> <user>       : Revoke access
  REQACCESS <file> -R/-W        : Request access from owner
  VIEWREQUESTS <file>           : View pending requests (Owner only)
  APPROVE <req_id>              : Approve a request
  DENY <req_id>                 : Deny a request
  MYREQUESTS                    : View status of your sent requests

Checkpoints & Folders:
  CHECKPOINT <file> <tag>       : Save current state as a checkpoint
  VIEWCHECKPOINT <file> <tag>   : Read a specific checkpoint
  REVERT <file> <tag>           : Revert file to a checkpoint
  LISTCHECKPOINTS <file>        : List all checkpoints for a file
  CREATEFOLDER <name>           : Create a new folder
  MOVE <file> <folder>          : Move a file into a folder
  VIEWFOLDER <name>             : List folder contents

General:
  VIEW [-l, -a]                 : List accessible files (-l for details and -a for admin view of all files)
  LIST                          : List all registered users
  EXIT / QUIT                   : Disconnect and exit
--------------------------

```
-----

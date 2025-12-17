# File Operations & Functionality Questions

## View File Operations

**Q1: Recursive file listing**
"How would we implement listing all files the user has access to? Should we search through all Storage Servers or maintain a cache?"

**Q2: Flag parsing**
"How do we parse command-line flags like '-a', '-l', '-al'? Should we handle them as separate flags or combinations?"

**Q3: File details calculation**
"To display word count and character count, do we need to fetch the entire file content? Is there a more efficient way?"

**Q4: Timestamp formatting**
"What's the best way to store and display timestamps? Should we use Unix timestamps, ISO format, or something else?"

**Q5: Table formatting**
"For displaying file lists with details in a table format, how do we handle files with long names or varying data sizes?"

**Q6: Performance of listing all files**
"If there are thousands of files, how do we efficiently list all of them? Would caching help?"

## Read File Operations

**Q7: Large file handling**
"What if a file is very large (100MB+)? Should we read it all at once or in chunks?"

**Q8: Buffering strategy**
"How should we buffer data when reading from a Storage Server? What buffer size is reasonable?"

**Q9: Network transmission**
"How do we send file content over TCP? Do we send the entire file as one message or break it into smaller packets?"

**Q10: Newline vs period delimiter**
"The spec uses periods as sentence delimiters, but how do we handle periods in abbreviations like 'Mr.' or 'e.g.'?"

**Q11: Special character handling**
"How do we handle special characters in file content? Should we validate content or accept anything?"

## Create File Operations

**Q12: Filename validation**
"What filenames should we allow or reject? Should there be restrictions on length, characters, etc.?"

**Q13: Duplicate filename handling**
"If a file already exists, what error should we return? Should we auto-increment the name like file(1).txt?"

**Q14: Default Storage Server selection**
"When creating a file, how does the Name Server choose which Storage Server to store it on? Should it be round-robin or load-based?"

**Q15: Metadata initialization**
"What metadata should be initialized when a file is created (owner, creation time, permissions)?"

**Q16: Atomic creation**
"Should file creation be atomic? What happens if the Name Server crashes between deciding to create and actually creating?"

## Write File Operations

**Q17: Sentence indexing**
"How do we index sentences? Is sentence 0 the first sentence or sentence 1? What about empty files?"

**Q18: Word indexing within sentences**
"Similar to sentences, how do we index words? Is word 0 the first word or word 1?"

**Q19: Multi-word insertion**
"When the user writes multiple words at once (like 'deeply mistaken hollow lil gei-fwen'), how do we split them?"

**Q20: Sentence splitting**
"When a user adds content that includes sentence delimiters (., !, ?), how do we detect and split sentences?"

**Q21: Out of bounds handling**
"The spec says invalid indices should return errors. What indices are valid? Can we insert at the end?"

**Q22: Word order after insertion**
"After inserting 'pocket-sized' at position 6 when there are only 5 words, do other words shift? How exactly?"

**Q23: ETIRW mechanism**
"ETIRW is 'WRITE' backwards. Is this just a signal to end the write operation, or does it need special handling?"

**Q24: Temporary swap file**
"The hint mentions using a temporary swap file. Why would we do this instead of modifying the original file directly?"

**Q25: Lock acquisition timing**
"When exactly is the sentence locked? When WRITE command is received or when the user starts entering data?"

**Q26: Lock timeout**
"What if a user locks a sentence with WRITE but never sends ETIRW? How long should we wait before timing out?"

**Q27: Concurrent WRITE operations**
"If user A is writing to sentence 0 and user B tries to write to sentence 1, is this allowed?"

**Q28: Transactional writes**
"Should all updates in a single WRITE operation be applied atomically, or individually?"

## Undo Mechanism

**Q29: Single level undo**
"The spec says only one level of undo is supported. Does this mean UNDO can only be called once per file, or once total?"

**Q30: Undo storage**
"Where should we store the previous state of a file for undo? In the Storage Server memory or on disk?"

**Q31: Undo across users**
"If user A modifies a file and user B calls UNDO, it should undo user A's changes. How do we track who made which change?"

**Q32: Undo after multiple operations**
"If a user writes to a file, then reads it, then writes again, which write does UNDO revert?"

**Q33: Undo metadata**
"What information should we store for undo? Just the previous file state, or also who made the change and when?"

## Delete File Operations

**Q34: Delete permission check**
"Should only the owner be able to delete a file? What if the owner grants write access to someone else?"

**Q35: In-use file deletion**
"Can we delete a file if someone is currently reading or writing to it? Should we return an error or wait?"

**Q36: Storage cleanup**
"When a file is deleted, how do we ensure the storage space is actually freed? Do we need garbage collection?"

**Q37: Cascading deletes**
"If we implement folders later, should deleting a folder delete all files in it?"

**Q38: ACL cleanup**
"When a file is deleted, should we clean up the access control list? Where is the ACL stored?"

**Q39: Replica deletion**
"If we implement replication, when we delete a file, should we delete it from all replicas?"

## File Info Operation

**Q40: Information consolidation**
"The INFO command needs to gather data from Name Server and possibly Storage Servers. How do we combine this information?"

**Q41: Last access tracking**
"How do we track when a file was last accessed? Should every READ operation update this?"

**Q42: Last modified tracking**
"How do we track when a file was last modified? Is it updated for every WRITE operation?"

**Q43: Size calculation**
"Should file size be stored as bytes, characters, or words? How do we calculate it efficiently?"

**Q44: Access information display**
"How do we display access rights for multiple users? Should we show all users with ANY access or only specific permissions?"

## Stream Operation

**Q45: 0.1 second delay implementation**
"How do we implement a 0.1 second delay between words in streaming? Should we use sleep() or a more precise timer?"

**Q46: Direct client-SS connection for streaming**
"Similar to READ, should streaming establish a direct connection with the Storage Server?"

**Q47: Stream interruption**
"If the Storage Server crashes mid-stream, how does the client know? What message format should we use?"

**Q48: Large file streaming**
"For a large file, streaming word-by-word could take a very long time. Is there a maximum file size for streaming?"

**Q49: Buffering during stream**
"Should the Storage Server send words one at a time, or buffer them and send in batches?"

**Q50: User interrupt during stream**
"If a user presses Ctrl+C during streaming, how do we cleanly interrupt the operation?"

## Execute File Operation

**Q51: Command execution environment**
"Should we execute commands in a shell (/bin/bash) or directly? What are the security implications?"

**Q52: Output capture**
"How do we capture stdout and stderr from executed commands? Should we combine them or keep separate?"

**Q53: Input to executed commands**
"If the file contains commands that expect input, how do we handle that?"

**Q54: Return codes**
"Should we capture and display the exit code of executed commands?"

**Q55: Security concerns**
"The file contains shell commands. What happens if someone executes a file with 'rm -rf /'? Do we need restrictions?"

**Q56: Execution timeout**
"What if a command in the file runs infinitely? Should we have a timeout?"

**Q57: Permission checks**
"Should only the owner be able to execute a file, or anyone with read access?"

**Q58: Name Server vs Storage Server execution**
"The spec says execution happens on the Name Server. Why not on the Storage Server?"

## Access Control Operations

**Q59: Owner privileges**
"The spec says owner always has both read and write access. How do we prevent an owner from accidentally removing their own access?"

**Q60: Permission escalation**
"If user A has read access and user B grants them write access, how do we update their access?"

**Q61: Transitive access**
"If user A has access to a file and grants access to user B, does user B also have access? (No, access is direct)"

**Q62: Empty permission handling**
"After REMACCESS, should we store an empty permission entry or remove it entirely?"

**Q63: Default access for creator**
"When a file is created, should other users have any default access, or only the creator?"

**Q64: Access audit trail**
"Should we log who granted/removed access and when?"

## List Users Operation

**Q65: User directory**
"Where is the list of all users stored? Is it on the Name Server or distributed?"

**Q66: Dynamic user registration**
"When a new client connects, is the user automatically registered? Or must they be pre-registered?"

**Q67: User deletion**
"If a user hasn't logged in for a while, should they be automatically removed from the system?"

**Q68: User identification**
"Is username alone enough to identify a user, or do we need authentication (passwords)?"

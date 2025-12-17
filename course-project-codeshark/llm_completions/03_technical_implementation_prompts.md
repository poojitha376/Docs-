# Technical Implementation Questions

## Data Structures & Algorithms

**Q1: File lookup efficiency**
"The spec requires O(n) or better for file lookups. What data structure gives us sub-linear time? Hash map vs Trie vs B-tree?"

**Q2: Trie for file names**
"How would we use a Trie to efficiently search for files? What are the space-time tradeoffs?"

**Q3: Hash map collisions**
"If we use a hash map for file lookup, how do we handle collisions? Should we use chaining or open addressing?"

**Q4: Cache eviction policy**
"The spec mentions caching recent searches. What eviction policy should we use? LRU, LFU, or something else?"

**Q5: LRU cache implementation**
"How would we implement an LRU cache using Python or C? What data structures do we need?"

**Q6: Access Control List representation**
"How should we represent ACL for multiple users and multiple permission types? Dictionary, set, or custom class?"

**Q7: Sentence parsing algorithm**
"How do we efficiently split a string by multiple delimiters (., !, ?)? Should we iterate character by character?"

**Q8: Word tokenization**
"After splitting sentences, how do we split into words? Is simple whitespace splitting enough?"

**Q9: Undo state storage**
"Should we store the entire previous file content for undo, or just the differences (delta)? What's more efficient?"

**Q10: File index structure**
"How do we index sentences and words efficiently? Just store offsets, or create a data structure?"

## Concurrency & Locking

**Q11: Lock data structure**
"How do we represent a lock on a sentence? Should we store which user owns the lock and when it was acquired?"

**Q12: Mutual exclusion**
"In a multi-threaded environment, how do we prevent two threads from acquiring the same lock? Mutexes, semaphores, monitors?"

**Q13: Deadlock prevention**
"Can deadlocks occur in this system? If user A locks sentence 0 while holding lock on sentence 1, and user B does the opposite?"

**Q14: Lock fairness**
"If multiple users want to write to the same sentence, who gets the lock? Should it be FIFO?"

**Q15: Read-write locks**
"Should we have different lock types for reading vs writing? Does a reader block other readers?"

**Q16: Lock release guarantees**
"How do we ensure a lock is released even if the client crashes? Timeout? Heart beat?"

**Q17: Concurrent read-write safety**
"If user A is reading a file while user B is writing to it, what happens? Do we need a separate read lock?"

**Q18: Transaction isolation levels**
"What isolation level do we guarantee? Can a reader see partially written data?"

**Q19: Thread pool sizing**
"If we use thread pool, how many threads should we create? Fixed size or dynamic?"

**Q20: Async vs sync write**
"The write operation is locked until ETIRW. Is this blocking or can we do other work?"

## File System Operations

**Q21: File persistence**
"Where do we physically store files? In a single directory or subdirectories per Storage Server?"

**Q22: Metadata persistence**
"Should metadata (owner, creation time, permissions) be stored in the same file or separately?"

**Q23: Directory organization**
"How do we organize files on disk? Flat directory or hierarchical?"

**Q24: Atomic file operations**
"How do we ensure creating or deleting a file is atomic on disk?"

**Q25: File path construction**
"If a Storage Server can host multiple files, how do we construct unique paths?"

**Q26: Temporary file cleanup**
"If we create temporary swap files during writes, how do we clean them up if the process crashes?"

**Q27: Disk space management**
"How do we check available disk space before creating files?"

**Q28: File permissions on disk**
"Should we use OS-level file permissions or our own access control?"

## Network Communication

**Q29: Socket programming in Python vs C**
"Which language is better for socket programming in this project? What are the tradeoffs?"

**Q30: Serialization format**
"How do we serialize Python objects to send over network? JSON, pickle, or custom format?"

**Q31: Message framing protocol**
"How do we know when one message ends and another begins? Length prefix, delimiter, or fixed size?"

**Q32: Request-response correlation**
"If multiple requests are sent before receiving responses, how do we match responses to requests?"

**Q33: Bidirectional communication**
"Can both client and server send data simultaneously? How do we avoid deadlock?"

**Q34: Socket timeouts**
"How should we set socket timeouts? Different timeout for different operations?"

**Q35: Keep-alive mechanism**
"Should we implement keep-alive to detect dead connections?"

**Q36: Graceful shutdown**
"How do we cleanly close sockets? Do we need to notify the other end?"

**Q37: Error messages over network**
"What error messages should we send? Just error code or detailed description?"

**Q38: Packet ordering**
"TCP preserves order, but what if we send multiple messages? Do they stay in order?"

## State Management & Consistency

**Q39: Distributed state consistency**
"If the Name Server maintains file lists and Storage Servers maintain actual files, how do we keep them consistent?"

**Q40: Write ordering**
"If multiple writes happen concurrently on different Storage Servers, what's the order?"

**Q41: View consistency**
"Does every client see the same view of the file system at any point in time?"

**Q42: Eventual consistency**
"Or do we allow eventual consistency where clients might see different states temporarily?"

**Q43: Client state expiration**
"How long should we keep a client entry if they disconnect? Forever, a timeout, or until next update?"

**Q44: File location cache**
"If a Storage Server crashes and we have replicas, how do we know about the replica?"

**Q45: Metadata staleness**
"If a client caches file metadata (like size), when should we refresh it?"

## Logging & Monitoring

**Q46: Log format**
"What information should we log? Timestamp, component, operation, parameters, result?"

**Q47: Log destination**
"Should logs go to console, files, or both?"

**Q48: Log levels**
"Should we have different log levels (DEBUG, INFO, WARNING, ERROR)?"

**Q49: Performance logging**
"Should we log operation latencies? How detailed should performance metrics be?"

**Q50: Circular log files**
"If we log to files, should we implement log rotation to prevent files from growing too large?"

## Testing & Debugging

**Q51: Test framework**
"What testing framework should we use? Unit tests, integration tests, or both?"

**Q52: Wireshark debugging**
"How do we use Wireshark to inspect our TCP packets? What should we look for?"

**Q53: Netcat testing**
"How do we use netcat to create client/server stubs for testing?"

**Q54: Mocking components**
"How do we test the Name Server without a real Storage Server?"

**Q55: Concurrent testing**
"How do we test concurrent scenarios? Manually create threads or use test utilities?"

**Q56: Reproducible failures**
"If a test fails rarely, how do we make it reproducible for debugging?"

**Q57: Memory leaks**
"How do we detect memory leaks? Valgrind, AddressSanitizer, or language tools?"

## Error Handling

**Q58: Exception handling in Python**
"Should we use exceptions or return error codes? Python best practices?"

**Q59: Error propagation**
"If a Storage Server returns an error, how should the Name Server handle it?"

**Q60: Timeout errors**
"When should we consider an operation timed out? After how many seconds?"

**Q61: Recovery from errors**
"For transient errors (network hiccup), should we retry? How many times?"

**Q62: Error message clarity**
"Should error messages be user-friendly or detailed for debugging?"

**Q63: Error logging**
"Should we log all errors or only unexpected ones?"

## Performance Optimization

**Q64: Caching strategies**
"What should we cache? File metadata, file contents, or search results?"

**Q65: Connection pooling**
"Should we maintain persistent connections between components or create new ones?"

**Q66: Batch operations**
"If a client sends multiple operations, should we batch them for efficiency?"

**Q67: Lazy loading**
"Should we load file contents only when needed or preload everything?"

**Q68: Compression**
"Should we compress files for network transmission?"

**Q69: Memory usage**
"How do we handle the case where total file size exceeds available RAM?"

**Q70: CPU utilization**
"Should we use multiple threads/processes for parallelism? What's the tradeoff?"

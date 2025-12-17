# Edge Cases, Protocol Details & Implementation Nuances

## Message Protocol Details

**Q1: Message framing**
"How do we know when a message ends? Fixed length? Delimiter? Length prefix?"

**Q2: Message ordering**
"Are messages guaranteed to arrive in order? If not, how do we handle it?"

**Q3: Serialization format**
"Should we encode messages in binary or text (like JSON)?"

**Q4: Protocol versioning**
"If we change the protocol, how do we handle old clients?"

**Q5: Message headers**
"What should every message contain? Operation type? Request ID?"

**Q6: Error messages**
"What format should error messages have?"

**Q7: Response format**
"Should all responses follow the same format?"

**Q8: Binary data**
"If a file contains binary data, how do we transmit it?"

**Q9: Large messages**
"How do we handle messages larger than available memory?"

**Q10: Message validation**
"How do we validate incoming messages are well-formed?"

## Connection Management

**Q11: Connection pooling**
"Should we reuse connections or open new ones each time?"

**Q12: Connection timeouts**
"How long should we wait for a response before timing out?"

**Q13: Idle connection cleanup**
"Should we close idle connections?"

**Q14: Reconnection logic**
"If connection drops, how many times should we retry connecting?"

**Q15: Connection persistence**
"Should clients maintain persistent connections or close after each operation?"

**Q16: Server accept queue**
"How many pending connections should we allow?"

**Q17: Maximum connections**
"Should we limit total concurrent connections?"

**Q18: Connection upgrade**
"If we add encryption, how do we upgrade existing connections?"

**Q19: Graceful shutdown**
"When shutting down, how do we handle active connections?"

**Q20: Connection identification**
"How do we identify which client sent a request?"

## Data Structure Edge Cases

**Q21: Empty sentence handling**
"Is an empty line a sentence? How many sentences are in '\\n\\n\\n'?"

**Q22: Sentence with only spaces**
"Is '   ' a sentence? A word?"

**Q23: Multiple delimiters**
"How do we handle '...' or '?!' (multiple sentence enders)?"

**Q24: Delimiter in word**
"Can a word contain a period like 'Dr.' or 'U.S.A.'?"

**Q25: Whitespace normalization**
"Should 'word1    word2' be treated as two words?"

**Q26: Tab characters**
"Should tabs be treated as delimiters like spaces?"

**Q27: Line ending inconsistency**
"Should we handle both \\n and \\r\\n?"

**Q28: File encoding**
"What if a file has non-UTF-8 characters?"

**Q29: Very large word**
"What if a word is 1MB? Can we handle it?"

**Q30: Null character**
"What if a file contains null bytes?"

## File System Edge Cases

**Q31: File size zero**
"Can we create a file with size 0? How do we READ it?"

**Q32: File modification during operation**
"If a file is modified while we're reading it, what happens?"

**Q33: Deleted file handling**
"If a file is deleted while being read, what happens?"

**Q34: Rapid creation/deletion**
"What if CREATE and DELETE happen simultaneously?"

**Q35: Rename during access**
"If a file is renamed while being accessed, what happens?"

**Q36: Permission change during access**
"If permissions change while user is accessing file, what happens?"

**Q37: File type conflicts**
"What if we try to treat a directory as a file?"

**Q38: Cross-filesystem operations**
"If files are on different Storage Servers, can we handle that?"

**Q39: File path length limits**
"Are there practical limits on path length?"

**Q40: Filename case sensitivity**
"On case-insensitive systems, does mouse.txt equal MOUSE.TXT?"

## Concurrent Access Edge Cases

**Q41: Simultaneous WRITE at same location**
"If two users WRITE at the exact same sentence, what happens?"

**Q42: WRITE then UNDO from different users**
"If user A writes, then user B UNDO, what happens?"

**Q43: READ during WRITE**
"If user A reads while user B writes, what does A see?"

**Q44: DELETE while reading**
"If user A deletes file while user B is reading, what happens?"

**Q45: Permission change during access**
"If permissions change mid-operation, do we abort?"

**Q46: Lock holder disconnects**
"If a user holding a lock disconnects, what happens to the lock?"

**Q47: Same user multiple connections**
"If the same user connects twice, are they separate or merged?"

**Q48: Conflicting operations**
"If two users try conflicting operations, who wins?"

**Q49: Transaction isolation**
"Can one user see partial results of another user's multi-step operation?"

**Q50: Fairness**
"If many users want a lock, is it fair (FIFO) or random?"

## Storage Server Edge Cases

**Q51: Storage Server down**
"If Storage Server crashes, how do clients know to retry?"

**Q52: Partial write failure**
"If write succeeds to disk but transmission fails, what happens?"

**Q53: Corrupted file on disk**
"If the file on disk is corrupted, how do we detect/handle it?"

**Q54: Disk full**
"If Storage Server runs out of disk space, what happens?"

**Q55: Name Server can't reach Storage Server**
"How long before we declare it dead?"

**Q56: Restarting Storage Server**
"After restart, should it have the same state?"

**Q57: Storage Server location change**
"If Storage Server moves to new IP, how do clients find it?"

**Q58: Multiple Storage Servers same address**
"Can two Storage Servers have same IP:port?"

**Q59: Storage Server port in use**
"If port is already in use, what error should we show?"

**Q60: Storage Server resource limits**
"What happens if Storage Server runs out of file descriptors?"

## Name Server Edge Cases

**Q61: Name Server crash**
"If Name Server crashes, can clients still access files?"

**Q62: Name Server recovery**
"After Name Server restarts, is metadata still valid?"

**Q63: Name Server metadata corruption**
"If Name Server metadata is corrupted, how do we recover?"

**Q64: Name Server scalability**
"If we have 1 million files, will Name Server work?"

**Q65: Name Server query delay**
"If Name Server is slow, how long can clients wait?"

**Q66: Name Server bottleneck**
"Is Name Server a single point of failure?"

**Q67: Metadata sync issues**
"If metadata gets out of sync with actual file state, how do we fix it?"

**Q68: User registration consistency**
"If user registers while system is failing, do they get registered?"

**Q69: File creation race condition**
"If two users create same filename simultaneously, what happens?"

**Q70: Stale metadata cache**
"If Name Server caches data, how do we invalidate it?"

## Client Edge Cases

**Q71: Client crash recovery**
"If client crashes with open file, what happens?"

**Q72: Client timeout**
"If client is slow, should we timeout their operations?"

**Q73: Client disconnect**
"If client suddenly disconnects, do we clean up locks?"

**Q74: Invalid client input**
"How do we handle completely invalid commands?"

**Q75: Client buffer overflow**
"What if client tries to read a huge file into memory?"

**Q76: Client resource limits**
"Should client limit memory/CPU usage?"

**Q77: Client version mismatch**
"If old client connects to new server, what happens?"

**Q78: Client IP spoofing**
"Should we verify client IP matches claimed user?"

**Q79: Multiple clients same user**
"Can same user connect from multiple clients?"

**Q80: Client-side caching**
"Should clients cache file data locally?"

## Timing & Synchronization Edge Cases

**Q81: System clock skew**
"If server clocks are out of sync, what happens?"

**Q82: Timestamp resolution**
"Should timestamps be milliseconds or seconds?"

**Q83: Causality ordering**
"Do we preserve causality between operations?"

**Q84: Wall clock vs logical clock**
"Which is better for ordering events?"

**Q85: Timezone handling**
"Should timestamps be UTC or local?"

**Q86: Daylight saving time**
"Does changing system clocks affect our operations?"

**Q87: Network latency variation**
"How do we handle variable network latencies?"

**Q88: Out of order delivery**
"If packets arrive out of order, how do we handle?"

**Q89: Duplicate packets**
"If same packet arrives twice, do we process twice?"

**Q90: Old packets**
"If very old packet arrives late, how do we handle?"

## Security Edge Cases

**Q91: Password in logs**
"Should we ever log passwords?"

**Q92: Permission escalation**
"Can user change their own permissions?"

**Q93: Admin user**
"Should we have a super-admin that can do anything?"

**Q94: Access revocation**
"If we revoke access mid-operation, what happens?"

**Q95: Audit trail tampering**
"Can users delete their own access logs?"

**Q96: Side-channel attacks**
"Can users infer file existence from response times?"

**Q97: Timing attacks**
"Should we use constant-time comparisons for passwords?"

**Q98: Replay attacks**
"Can attacker replay an old request to repeat operation?"

**Q99: Man-in-the-middle**
"Are messages encrypted to prevent interception?"

**Q100: Authorization bypass**
"How do we prevent direct Storage Server access?"

## Performance Edge Cases

**Q101: Thundering herd**
"If all clients reconnect simultaneously, what happens?"

**Q102: Cache stampede**
"If cache expires, do all requests regenerate it?"

**Q103: Lock contention**
"If many users access same file, does lock become bottleneck?"

**Q104: Memory leak in long-running server**
"How do we detect/prevent memory leaks?"

**Q105: Socket exhaustion**
"What happens if we run out of file descriptors?"

**Q106: CPU spinning**
"Should we use busy-waiting or proper synchronization?"

**Q107: Disk I/O bottleneck**
"How do we handle when disk is slow?"

**Q108: Network saturation**
"What happens if network is fully utilized?"

**Q109: Garbage collection pause**
"Do GC pauses affect responsiveness?"

**Q110: Buffer size tuning**
"What's optimal buffer size for network operations?"

## Specification Interpretation Questions

**Q111: 'Sentence' definition**
"Does specification clearly define what a sentence is?"

**Q112: 'Word' definition**
"Does specification clearly define what a word is?"

**Q113: Lock semantics**
"Are locks exclusive? Can they be shared?"

**Q114: Atomic operations**
"Which operations must be atomic?"

**Q115: Consistency model**
"What consistency do we guarantee?"

**Q116: Partition tolerance**
"What happens during network partitions?"

**Q117: Replication semantics**
"Does replication need to be synchronous?"

**Q118: Checkpoint semantics**
"Can checkpoints be arbitrary points or just complete states?"

**Q119: STREAM operation timing**
"Does 'every 500ms' mean exactly or approximately?"

**Q120: Access request expiration**
"If spec is silent on expiration, should requests expire?"

## Bonus Feature Questions

**Q121: Folder implementation complexity**
"Is implementing folders worth 50 bonus marks?"

**Q122: Checkpoint complexity**
"How many checkpoints should we keep?"

**Q123: Access request workflow**
"Should access requests send notifications?"

**Q124: Fault tolerance testing**
"How do we test fault tolerance in class?"

**Q125: Feature trade-offs**
"If time is short, which bonus features are easiest?"

**Q126: Partial bonus credit**
"Do we get partial credit for partially implemented features?"

**Q127: Feature priority**
"Should we finish core features first or balance with bonus?"

**Q128: Scope management**
"How much bonus should we attempt?"

## Evaluation Logistics

**Q129: Test environment**
"What OS/Python version will the evaluation use?"

**Q130: Network setup**
"Will all components run on same machine or different?"

**Q131: Configuration expectations**
"How should we handle ports and IP addresses?"

**Q132: Test data**
"What test data will be used for evaluation?"

**Q133: Time limits**
"Are there time limits for operations?"

**Q134: Script submission**
"Should we provide scripts to start the system?"

**Q135: Source code submission**
"What files should we submit?"

**Q136: Documentation submission**
"What documentation is required?"

**Q137: Presentation format**
"Should we present/demo our system?"

**Q138: Grading rubric**
"Can we see the detailed grading rubric?"

**Q139: Regrading policy**
"Can we request regrading if we disagree?"

**Q140: Late submission**
"What's the penalty for late submission?"

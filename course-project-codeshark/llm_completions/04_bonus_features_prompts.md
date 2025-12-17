# Bonus Features & Advanced Topics

## Hierarchical Folder Structure

**Q1: Folder representation**
"How should we represent folders? As special files or separate entities in the Name Server?"

**Q2: Nested path handling**
"How do we handle paths like /folder1/subfolder/file.txt? Should we parse them component by component?"

**Q3: Folder permissions**
"If a user has access to a folder, do they automatically have access to all files in it?"

**Q4: Moving files across servers**
"If we MOVE a file to a folder on a different Storage Server, how do we handle the transfer?"

**Q5: Folder deletion**
"Should DELETE on a folder delete all files recursively, or fail if the folder isn't empty?"

**Q6: Symbolic links**
"Should we support symbolic links? Or just hard hierarchies?"

**Q7: Current directory concept**
"Should clients have a current directory to make path navigation easier?"

**Q8: Relative vs absolute paths**
"Should we support both /path/to/file (absolute) and ./file (relative) syntax?"

## Checkpoints Feature

**Q9: Checkpoint storage**
"Where do we store checkpoints? Separate from the main file or as file versions?"

**Q10: Checkpoint naming**
"The user can name checkpoints. Should names be unique globally or per file?"

**Q11: Checkpoint tagging**
"Should checkpoints store metadata like creation time and creator?"

**Q12: Reverting to checkpoint**
"When reverting to a checkpoint, should it save the current state as a checkpoint?"

**Q13: Checkpoint deletion**
"Can users delete old checkpoints? Or are all checkpoints permanent?"

**Q14: Checkpoint comparison**
"Should we support viewing differences between checkpoints?"

**Q15: Storage efficiency for checkpoints**
"If we store full copies of files for each checkpoint, storage could explode. Should we use delta compression?"

**Q16: Checkpoint versioning**
"Should we limit the number of checkpoints per file?"

## Access Request System

**Q17: Request storage**
"Where do we store access requests? In the Name Server or on the Storage Server?"

**Q18: Request notification**
"Should the owner get notified when someone requests access?"

**Q19: Request expiration**
"Should access requests expire if not approved/denied quickly?"

**Q20: Bulk approvals**
"Can an owner approve multiple requests at once?"

**Q21: Request history**
"Should we keep a history of approved/denied requests?"

**Q22: Permission levels in requests**
"Can a user request specific permission levels (read-only vs read-write)?"

**Q23: Request cancellation**
"Can a user cancel their own access request?"

## Fault Tolerance & Replication

**Q24: Replica selection**
"When replicating a file to another Storage Server, which server should we choose?"

**Q25: Asynchronous replication**
"The spec says asynchronous writes. How do we know replication completed?"

**Q26: Write acknowledgment**
"For a write operation, should we wait for replication to complete before acknowledging?"

**Q27: Replica consistency**
"If the Name Server crashes mid-replication, how do we ensure replicas are consistent?"

**Q28: Failure detection mechanism**
"How does the Name Server detect that a Storage Server has failed? Heartbeat interval?"

**Q29: Stale replica handling**
"If a Storage Server crashes and comes back online, how do we sync it with replicas?"

**Q30: Read from replica**
"If the primary Storage Server is down, can clients read from the replica?"

**Q31: Write to replica**
"Can clients write to a replica, or only to the primary?"

**Q32: Replica promotion**
"If the primary fails permanently, should we promote a replica to primary?"

**Q33: Replication overhead**
"What's the storage overhead of replication? 2x for one replica?"

**Q34: Selective replication**
"Should we replicate all files or only important ones?"

**Q35: Split-brain problem**
"If network partitions and Name Server can't reach a Storage Server, how do we prevent conflicting writes?"

## Advanced Concurrency

**Q36: Optimistic locking**
"Instead of locking, could we detect conflicts after writes?"

**Q37: Conflict resolution**
"If two users try to modify the same sentence concurrently, how do we resolve conflicts?"

**Q38: Operational transformation**
"Could we use Operational Transformation (like Google Docs) for concurrent editing?"

**Q39: Version vectors**
"Should we track version vectors for causality?"

**Q40: Write-write conflicts**
"How do we handle write-write conflicts when users modify the same sentence?"

**Q41: Read-your-write consistency**
"Should a client see their own writes immediately or eventually?"

**Q42: Serializability**
"Do we guarantee serializability of transactions?"

## Performance & Scalability

**Q43: Load balancing**
"If multiple Storage Servers exist, how do we distribute files to balance load?"

**Q44: Server capacity**
"How do we know when a Storage Server is nearing capacity?"

**Q45: Horizontal scaling**
"Can we add more Storage Servers to handle more data?"

**Q46: Query optimization**
"Should we optimize how we query files across multiple Storage Servers?"

**Q47: Data migration**
"If we need to rebalance, how do we migrate files between servers?"

**Q48: Sharding**
"Should we split files across multiple Storage Servers (sharding)?"

**Q49: Caching invalidation**
"When a file is updated, how do we invalidate cached copies?"

**Q50: Compression**
"Should we compress files for storage?"

## Security Considerations

**Q51: Authentication**
"Should we require passwords or just usernames?"

**Q52: Authorization**
"How do we enforce that a user can only perform operations they're authorized for?"

**Q53: Data encryption**
"Should file data be encrypted at rest or in transit?"

**Q54: Command injection**
"When executing files, how do we prevent command injection attacks?"

**Q55: Information disclosure**
"Should users see metadata of files they don't have access to?"

**Q56: Audit logging**
"Should we log all file access for audit purposes?"

**Q57: Rate limiting**
"Should we limit how many operations a user can perform?"

**Q58: Backup and recovery**
"Should we implement backups? Off-site storage?"

**Q59: Data retention**
"How long should we keep deleted files? Forever or trash bin?"

## Monitoring & Observability

**Q60: Metrics collection**
"What metrics should we track? Operations/sec, latency, error rate?"

**Q61: Dashboards**
"Should we create dashboards to visualize system health?"

**Q62: Alerting**
"Should we alert operators when something goes wrong?"

**Q63: Distributed tracing**
"Should we trace requests across multiple components?"

**Q64: SLA monitoring**
"Should we monitor and report on service level objectives?"

## Edge Cases & Gotchas

**Q65: Empty file handling**
"How do we handle empty files? Can they be written to?"

**Q66: Single character files**
"What if a file contains just one character? How do we sentence-split?"

**Q67: File with only delimiters**
"What if a file is just '....' (four periods)? How many sentences?"

**Q68: Whitespace handling**
"Should we preserve leading/trailing whitespace? Trim it?"

**Q69: Unicode characters**
"Should we support Unicode or just ASCII?"

**Q70: Very long sentences**
"What if a sentence is millions of words? How do we index efficiently?"

**Q71: Very long words**
"What if a 'word' is millions of characters?"

**Q72: Name collisions**
"Can two files have the same name in different folders?"

**Q73: Reserved words**
"Should we prevent files from being named 'CON', 'PRN', etc.?"

**Q74: Case sensitivity**
"Are filenames case-sensitive? Is mouse.txt the same as Mouse.txt?"

**Q75: Special characters in names**
"Should we allow files named 'file*name.txt' or 'file/.txt'?"

## Implementation Strategies

**Q76: Language choice**
"Should we implement in Python or C? What are the considerations?"

**Q77: Framework usage**
"Should we use a framework like Flask for the server or raw sockets?"

**Q78: Library dependencies**
"What external libraries are acceptable to use?"

**Q79: Code organization**
"How should we structure our codebase for multiple components?"

**Q80: Configuration management**
"How do we manage configuration (IP addresses, ports, timeouts)?"

**Q81: Deployment**
"How do we deploy and run multiple components on the same/different machines?"

**Q82: Team collaboration**
"How do we avoid merge conflicts when multiple team members code?"

**Q83: Integration testing**
"How do we test multiple components working together?"

**Q84: Documentation**
"What documentation should we provide for our implementation?"

**Q85: Code review**
"How should team members review each other's code?"

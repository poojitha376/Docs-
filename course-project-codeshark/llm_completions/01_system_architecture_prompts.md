# System Architecture & Design Questions

## Overall System Understanding

**Q1: High-level architecture overview**
"Can you explain the overall architecture of a distributed file system like NFS? What are the main components and how do they communicate with each other?"

**Q2: Component responsibilities**
"What are the key responsibilities of a Name Server vs a Storage Server in a distributed file system? How do they differ from the client?"

**Q3: Client-server communication patterns**
"In a distributed system, when should a client communicate directly with a server vs through a middleman? What are the pros and cons?"

**Q4: Single point of failure**
"The spec says Name Server failure means the whole system goes down. Why is this a single point of failure? What would be the implications for a real system?"

**Q5: Concurrent access challenges**
"What are the main challenges when multiple clients try to access the same file simultaneously? How do we prevent data corruption?"

**Q6: Sentence-level locking rationale**
"Why lock at the sentence level instead of the word level or the entire file level? What's the tradeoff?"

**Q7: Direct vs indirect communication**
"For READ operations, why does the spec say clients should connect directly to storage servers instead of going through the Name Server?"

**Q8: Metadata vs data separation**
"The Name Server maintains metadata like file locations, while Storage Servers store actual data. Why is this separation important?"

## Design Patterns

**Q9: Request-response pattern**
"What's the difference between synchronous and asynchronous communication in distributed systems? Which should we use for different operations?"

**Q10: Callback mechanism**
"How can we implement a callback mechanism where a Storage Server tells the Name Server about new files when it starts up?"

**Q11: Directory structure representation**
"What data structure is best for maintaining a mapping between file names and their storage locations? Should we use a hash map, tree, or trie?"

**Q12: Access control list design**
"How should we represent and store access control information? What's the most efficient way to check if a user has permissions?"

## State Management

**Q13: Client state tracking**
"What information does the Name Server need to track about connected clients? For how long should it keep this information?"

**Q14: File state tracking**
"What metadata should be stored for each file (creation time, last modified, owner, access history, etc.)? Where should this metadata live?"

**Q15: Write operation atomicity**
"When a client writes to a file, how do we ensure that if something goes wrong mid-write, the file state remains consistent?"

**Q16: Consistency guarantees**
"What level of consistency do we need to guarantee? If user A writes something, and user B reads immediately after, will they see the changes?"

## Error Handling Strategy

**Q17: Error codes design**
"What should our universal error code system look like? How do we categorize different types of errors (auth, not found, locked, etc.)?"

**Q18: Failure detection approach**
"How would the Name Server know if a Storage Server has gone down? What are different approaches (heartbeat, timeout, etc.)?"

**Q19: Graceful degradation**
"If a Storage Server crashes mid-operation, how should the system respond? Should we retry, fail immediately, or wait?"

**Q20: Client reconnection handling**
"If a client disconnects and reconnects, should the system treat it as the same client or a new one? How do we handle incomplete operations?"

## Network Communication

**Q21: TCP vs UDP for this use case**
"Why is TCP better than UDP for a file system? What guarantees does TCP provide that we need?"

**Q22: Message framing**
"How do we know where one message ends and another begins in TCP? Do we need a header with message length?"

**Q23: Timeout handling**
"How long should we wait for a response before assuming a server is down? What if the network is just slow?"

**Q24: Packet loss handling**
"TCP handles packet loss, but how do we handle it at the application level for our specific use case?"

## IP and Port Management

**Q25: Known IP/port for Name Server**
"The spec says Name Server IP/port are 'publicly known'. In practice, how would this work? Where is this information stored/configured?"

**Q26: Dynamic port assignment**
"When a new Storage Server starts, how does it know what port to use for accepting connections? Should ports be pre-assigned or dynamic?"

**Q27: Port collision handling**
"What happens if two components try to use the same port? How do we prevent or detect this?"

**Q28: Multiple network interfaces**
"What if a machine has multiple network interfaces/IP addresses? Which one should components advertise?"

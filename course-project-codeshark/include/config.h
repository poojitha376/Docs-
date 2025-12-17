#ifndef CONFIG_H
#define CONFIG_H

/* Network Configuration */
#define NM_HOST "192.168.1.100"        // Change this to the NM's IP address
#define NM_PORT 9001                    // Remains the same

#define SS_HOST "0.0.0.0"               // SS listens on all interfaces
#define SS_PORT_BASE 9002               // SS client-facing port (per instance)

#define CLIENT_HOST "0.0.0.0"           // Not used for client (client connects out)

#endif // CONFIG_H
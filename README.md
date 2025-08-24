# Chat Application Based on Linux Socket
A simple **socket-based chat application** written in C for Linux.  
This project demonstrates **client-server communication** using TCP sockets, modularized with static libraries, and includes logging, queue management, and time utilities.

## Workflow / Architecture

The chat application follows a simple **client-server model**:

1. **Server must be started first**  
   - Listens for incoming client connections.  
   - Maintains a list of active clients.  
   - Handles connection requests between clients.

2. **Clients connect to the server**  
   - Each client registers itself with the server.  
   - Clients can set their username and request the list of active users.  
   - Clients can initiate direct chat sessions with other connected clients.
     
```text
+---------+          +----------+          +---------+
| Client1 | <------> |  Server  | <------> | Client2 |
+---------+          +----------+          +---------+

```
---
### Supported Commands (Client)

1. **get_list**  
   Get the list of currently available clients on the server.

2. **set_name <name>**  
   Register a new name with the server.  
   _(If not set, the server assigns a default name when the client connects.)_

3. **connect <name>**  
   Connect to another client on the server for chatting.

4. **disconnect**  
   End the active chat session (if currently connected to a peer).
---

## Build Instructions
### 1. Build Server
```bash
cd server
make
```

### 2. Build Client
```bash
cd client
make

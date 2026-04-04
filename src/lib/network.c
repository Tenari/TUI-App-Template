#include "../base/all.h"

// https://stackoverflow.com/questions/1098897/what-is-the-largest-safe-udp-packet-size-on-the-internet
#define UDP_MAX_MESSAGE_LEN 508
#ifndef NET_OUTGOING_MESSAGE_QUEUE_LEN
#define NET_OUTGOING_MESSAGE_QUEUE_LEN 16
#endif

#ifndef NET_SERVER_MAX_CLIENTS
#define NET_SERVER_MAX_CLIENTS (16)
#endif

typedef struct sockaddr_in SocketAddress;

typedef struct TCPServer {
  bool ready;
  i32 socket_fd;
  SocketAddress address;
} TCPServer;

typedef struct UDPServer {
  bool ready;
  SocketAddress server_address;
  i32 server_socket;
} UDPServer;

typedef struct MultiServer {
  TCPServer tcp_server;
  UDPServer udp_server;
} MultiServer;

typedef struct ServableUDPInfo {
  UDPServer server;
  void (*callback)(u8* udp_message, i32 udp_len, SocketAddress sending_address, i32 socket);
} ServableUDPInfo;

typedef struct TCPClient {
  bool ready;
  SocketAddress server_address;
  i32 socket;
} TCPClient;

typedef struct UDPClient {
  bool ready;
  SocketAddress server_address;
  u16 client_port;
  i32 socket;
} UDPClient;

typedef struct MultiClient {
  TCPClient tcp_client;
  UDPClient udp_client;
} MultiClient;

typedef struct NetworkMessage {
  bool tcp; // default = false = UDP, just sendto(address)
  u16 bytes_len;
  i32 socket_fd;
  char* long_bytes; // used when message is longer than UDP max, memory must be managed by caller
  SocketAddress address;
  u8 bytes[UDP_MAX_MESSAGE_LEN];
} NetworkMessage;

typedef struct OutgoingMessageQueue {
  NetworkMessage items[NET_OUTGOING_MESSAGE_QUEUE_LEN];
  u32 head;
  u32 tail;
  u32 count;
  Mutex mutex;
  Cond not_empty;
  Cond not_full;
} OutgoingMessageQueue;

typedef void (*HandleMessageCb)(u8* udp_message, i32 bytes_recieved, SocketAddress sending_address, i32 socket);
fn OutgoingMessageQueue* newOutgoingMessageQueue(Arena* a) {
  OutgoingMessageQueue* result = arenaAlloc(a, sizeof(OutgoingMessageQueue));
  MemoryZero(result, (sizeof *result));
  result->mutex = newMutex();
  result->not_full = newCond();
  result->not_empty = newCond();
  return result;
}

fn void outgoingMessageQueuePush(OutgoingMessageQueue* queue, NetworkMessage* msg) {
  lockMutex(&queue->mutex); {
    while (queue->count == NET_OUTGOING_MESSAGE_QUEUE_LEN) {
      waitForCondSignal(&queue->not_full, &queue->mutex);
    }

    MemoryCopy(&queue->items[queue->tail], msg, (sizeof *msg));
    queue->tail = (queue->tail + 1) % NET_OUTGOING_MESSAGE_QUEUE_LEN;
    queue->count++;

    signalCond(&queue->not_empty);
  } unlockMutex(&queue->mutex);
}

fn NetworkMessage* outgoingMessageNonblockingQueuePop(OutgoingMessageQueue* q, NetworkMessage* copy_target) {
  // immediately returns NULL if there's nothing in the ThreadQueue
  // copies the ParsedClientCommand into `copy_target` if there is something in the queue
  // and marks it as popped from the queue
  NetworkMessage* result = NULL;

  lockMutex(&q->mutex); {
    if (q->count > 0) {
      result = &q->items[q->head];
      MemoryCopy(copy_target, result, (sizeof *copy_target));
      q->head = (q->head + 1) % NET_OUTGOING_MESSAGE_QUEUE_LEN;
      q->count--;

      signalCond(&q->not_full);
    }
  } unlockMutex(&q->mutex);

  return result;
}

fn NetworkMessage* outgoingMessageQueuePop(OutgoingMessageQueue* q, NetworkMessage* copy_target) {
  NetworkMessage* result = NULL;

  lockMutex(&q->mutex); {
    while (q->count == 0) {
        waitForCondSignal(&q->not_empty, &q->mutex);
    }

    result = &q->items[q->head];
    MemoryCopy(copy_target, result, (sizeof *copy_target));
    q->head = (q->head + 1) % NET_OUTGOING_MESSAGE_QUEUE_LEN;
    q->count--;

    signalCond(&q->not_full);
  } unlockMutex(&q->mutex);

  return result;
}

fn bool socketAddressEqual(SocketAddress a, SocketAddress b) {
  return a.sin_addr.s_addr == b.sin_addr.s_addr
    && a.sin_port == b.sin_port;
}

i32 netRecvExact(i32 socket, void* buf, u16 bytes_to_recv) {
  i32 got, got_this_iter;
  for (got = 0; got < bytes_to_recv; got += got_this_iter) {
    got_this_iter = recv(socket, (char*)buf + got, bytes_to_recv - got, 0);
    if (got_this_iter <= 0) return got_this_iter;
  }
  return got;
}

i32 netRecvMessage(i32 socket, u8* message_buffer, void (*addSystemMessage)(u8* msg)) {
  i32 bytes_recieved;
  u16 msg_len;
  i32 first_recv_got = netRecvExact(socket, &msg_len, 2);
  if (first_recv_got <= 0) {
    return first_recv_got;
  }
  msg_len = ntohs(msg_len); // parse it to our correct byte order

  bytes_recieved = netRecvExact(socket, message_buffer, msg_len);
  if (addSystemMessage != NULL) {
    char sbuf[128] = {0};
    sprintf(sbuf, "bytes_recieved=%d\n", bytes_recieved);
    addSystemMessage((u8*)sbuf);
  }
  return bytes_recieved;
}

// ONLY WORKS ON POSIX. taken from https://gist.github.com/miekg/a61d55a8ec6560ad6c4a2747b21e6128

// the only real difference between a udp "server" and a "client" is the bind() syscall
// that the server makes in order to specify a port/address that it's listening on
UDPServer netCreateUDPServer(u16 server_port) {
  UDPServer result = {0};
  // define the address we'll be listening on
  result.server_address.sin_family = AF_INET;
	result.server_address.sin_addr.s_addr = inet_addr("0.0.0.0");//htonl(INADDR_ANY);
	result.server_address.sin_port = htons(server_port);

  // get a FileDescriptor number from the OS to use for our socket
  result.server_socket = socket(PF_INET, SOCK_DGRAM, 0);
  if (result.server_socket < 0) {
    return result;
  }
  // to let us immediately kill and restart server
  i32 optval = 1;
	setsockopt(result.server_socket, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(i32));

  result.ready = bind(result.server_socket, (struct sockaddr *)&result.server_address, sizeof(result.server_address)) >= 0;

  return result;
}

UDPClient netCreateUDPClient(u16 server_port, str addr) {
  UDPClient result = {0};
  // define the address we'll be listening on
  result.server_address.sin_family = AF_INET;
  if (addr == 0) {
    result.server_address.sin_addr.s_addr = inet_addr("127.0.0.1");
  } else {
    result.server_address.sin_addr.s_addr = inet_addr(addr);
  }
	result.server_address.sin_port = htons(server_port);

  // get a FileDescriptor number from the OS to use for our socket
  result.socket = socket(PF_INET, SOCK_DGRAM, 0);
  if (result.socket < 0) {
    return result;
  }

  struct sockaddr_in client_address = {0};
  client_address.sin_family = AF_INET;
  client_address.sin_addr.s_addr = htonl(INADDR_ANY);
  client_address.sin_port = 0;
  result.ready = bind(result.socket, (struct sockaddr *)&client_address, sizeof(client_address)) >= 0;
  struct sockaddr_in empty_addr;
  socklen_t addr_len = sizeof(empty_addr);
  getsockname(result.socket, (struct sockaddr *)&empty_addr, &addr_len);
  result.client_port = ntohs(empty_addr.sin_port);
  return result;
}

void netInfiniteReadUDPClient(UDPClient* client, HandleMessageCb handleMessage) {
  u8 message_buffer[UDP_MAX_MESSAGE_LEN] = {0};
  i32 bytes_recieved = 0;
  SocketAddress client_address = {0};
  i32 addrlen = sizeof(struct sockaddr);
  while (true) {
    bytes_recieved = recvfrom(client->socket, message_buffer, UDP_MAX_MESSAGE_LEN, 0, (struct sockaddr *)&client_address, (socklen_t*)&addrlen);
    handleMessage(message_buffer, bytes_recieved, client_address, client->socket);
    MemoryZero(message_buffer, UDP_MAX_MESSAGE_LEN);
  }
}

void netInfiniteReadUDPServer(UDPServer* server, HandleMessageCb handleMessage) {
  u8 message_buffer[UDP_MAX_MESSAGE_LEN] = {0};
  i32 bytes_recieved = 0;
  SocketAddress client_address = {0};
  i32 addrlen = sizeof(struct sockaddr);
  while (true) {
    bytes_recieved = recvfrom(server->server_socket, message_buffer, UDP_MAX_MESSAGE_LEN, 0, (struct sockaddr *)&client_address, (socklen_t*)&addrlen);
    handleMessage(message_buffer, bytes_recieved, client_address, server->server_socket);
    MemoryZero(message_buffer, UDP_MAX_MESSAGE_LEN);
  }
}

TCPClient netCreateTCPClient(u16 server_port, str addr) {
  TCPClient result = {0};
  // define the address we'll be listening on
  result.server_address.sin_family = AF_INET;
  if (addr == 0) {
    result.server_address.sin_addr.s_addr = inet_addr("127.0.0.1");
  } else {
    result.server_address.sin_addr.s_addr = inet_addr(addr);
  }
	result.server_address.sin_port = htons(server_port);

  // get a FileDescriptor number from the OS to use for our socket
  result.socket = socket(PF_INET, SOCK_STREAM, 0);
  if (result.socket < 0) {
    return result;
  }
  socklen_t addr_len = sizeof(struct sockaddr_in);
  i32 connect_result = connect(result.socket, (struct sockaddr *)&result.server_address, addr_len);
  result.ready = connect_result != -1;

  return result;
}

bool netReconnectTCPClient(TCPClient* client) {
  socklen_t addr_len = sizeof(struct sockaddr_in);
  client->socket = socket(PF_INET, SOCK_STREAM, 0);
  if (client->socket < 0) {
    return false;
  }
  i32 connect_result = connect(client->socket, (struct sockaddr *)&client->server_address, addr_len);
  client->ready = connect_result != -1;
  return client->ready;
}

TCPServer netCreateTCPServer(u16 server_port) {
  TCPServer result = {0};
  // define the address we'll be listening on
  result.address.sin_family = AF_INET;
	result.address.sin_addr.s_addr = inet_addr("0.0.0.0");//htonl(INADDR_ANY);
	result.address.sin_port = htons(server_port);

  // get a FileDescriptor number from the OS to use for our TCP socket
  result.socket_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (result.socket_fd < 0) {
    return result;
  }
  // to let us immediately kill and restart server
  i32 optval = 1;
	setsockopt(result.socket_fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(i32));

  // bind() the TCP
  result.ready = bind(result.socket_fd, (struct sockaddr *)&result.address, sizeof(result.address)) >= 0;
  if (result.ready) {
    result.ready = result.ready && (listen(result.socket_fd, 10) >= 0);
  }

  return result;
}

void netInfiniteReadTCPServer(
  TCPServer* server,
  bool* should_quit,
  HandleMessageCb handleMessage,
  void (*closeConnection)(i32 socket_fd),
  void (*addSystemMessage)(u8* msg)
) {
  u8 message_buffer[UDP_MAX_MESSAGE_LEN] = {0};
  i32 bytes_recieved = 0;
  SocketAddress client_address = {0};
  i32 addrlen = sizeof(struct sockaddr);
  i32 new_fd;
  struct pollfd pollable_fds[NET_SERVER_MAX_CLIENTS+1] = {0}; // +1 for the listener socket
  // poll the `listen()`ed socket_fd
  pollable_fds[0].fd = server->socket_fd;
  pollable_fds[0].events = POLLIN;
  u32 pollable_fd_count = 1;

  i32 poll_event_count;
  while (*should_quit == false) {
    poll_event_count = poll(pollable_fds, pollable_fd_count, 3000); // times out after 3 seconds so that quitting the server will actually quit the server process in relatively short order.
    if (poll_event_count == -1) {
      // TODO handle error better
      *should_quit = true;
      continue;
    }
    for(u32 i = 0; i < pollable_fd_count; i++) {
      bool is_fd_readable = pollable_fds[i].revents & (POLLIN | POLLHUP);
      if (is_fd_readable) {
        bool is_fd_main_server_listener = pollable_fds[i].fd == server->socket_fd;
        if (is_fd_main_server_listener) { // it's a new connection
          new_fd = accept(server->socket_fd, (struct sockaddr *)&client_address, (socklen_t*)&addrlen);
          if (new_fd == -1) {
            if (addSystemMessage != NULL) {
              addSystemMessage((u8*)"TODO: handle this accept() error for real, bitch");
            }
          } else {
            if (pollable_fd_count < NET_SERVER_MAX_CLIENTS+1) {
              pollable_fds[pollable_fd_count].fd = new_fd;
              pollable_fds[pollable_fd_count].events = POLLIN | POLLHUP;
              pollable_fds[pollable_fd_count].revents = 0;
              pollable_fd_count++;
              if (addSystemMessage != NULL) {
                char sbuf[128] = {0};
                sprintf(sbuf, "new connection on socket=%d, total=%d\n", new_fd, pollable_fd_count);
                addSystemMessage((u8*)sbuf);
              }
            } else {
              send(new_fd, "server full", 11, 0); // go away sir, we are out of space to keep track of this socket
              close(new_fd);
            }
          }
        } else {// Otherwise we're just a regular client
          bytes_recieved = netRecvMessage(pollable_fds[i].fd, message_buffer, addSystemMessage);
          if (bytes_recieved <= 0) { // error condition
            bool client_hung_up = bytes_recieved == 0;
            // TODO do something with the error case
            closeConnection(pollable_fds[i].fd);
            close(pollable_fds[i].fd);
            if (addSystemMessage != NULL) {
              char sbuf[256] = {0};
              sprintf(sbuf, "closed connection on socket=%d, client_hung_up? %s\n", pollable_fds[i].fd, client_hung_up ? "yes" : "no");
              addSystemMessage((u8*)sbuf);
            }
            // copy the last one over the current one and "forget" the last one by decrementing the count
            pollable_fds[i] = pollable_fds[--pollable_fd_count];
          } else { // we got an actual message from this guy
            handleMessage(message_buffer, bytes_recieved, client_address, pollable_fds[i].fd);
          }
          MemoryZero(message_buffer, UDP_MAX_MESSAGE_LEN);
        }
      }
    }
  }
}

void netInfiniteReadTCPClient(
  TCPClient *client,
  bool* should_quit,
  HandleMessageCb handleMessage,
  void (*addSystemMessage)(u8* msg)
) {
  u8 message_buffer[UDP_MAX_MESSAGE_LEN] = {0};
  i32 bytes_recieved = 0;
  bool got_connection_error = false;
  while ((*should_quit) == false && got_connection_error == false) {
    bytes_recieved = netRecvMessage(client->socket, message_buffer, addSystemMessage);
    if (bytes_recieved == -1) {
      if (addSystemMessage != NULL) {
        char sbuf[128] = {0};
        sprintf(sbuf, "TODO handle this error, bytes_recieved=%d\n", bytes_recieved);
        addSystemMessage((u8*)sbuf);
      }
      got_connection_error = true;
      client->ready = false;
      close(client->socket);
      continue;
    }

    handleMessage(message_buffer, bytes_recieved, client->server_address, client->socket);
    MemoryZero(message_buffer, UDP_MAX_MESSAGE_LEN);
  }
}

MultiServer netCreateMultiServer(u16 server_port) {
  MultiServer result = {0};
  result.tcp_server = netCreateTCPServer(server_port);
  result.udp_server = netCreateUDPServer(server_port);
  return result;
}

MultiClient netCreateMultiClient(u16 server_port, str addr) {
  MultiClient result = {0};
  result.tcp_client = netCreateTCPClient(server_port, addr);
  result.udp_client = netCreateUDPClient(server_port, addr);
  return result;
}

// creates a new thread for listening for UDP and infinite loop on this thread for the TCP server
void netInfiniteReadMultiServer(
  MultiServer* server,
  bool* should_quit,
  HandleMessageCb handleMessage,
  void (*closeConnection)(i32 socket_fd),
  void (*addSystemMessage)(u8* msg)
) {
  u8 message_buffer[UDP_MAX_MESSAGE_LEN] = {0};
  i32 bytes_recieved = 0;
  SocketAddress client_address = {0};
  i32 addrlen = sizeof(struct sockaddr);
  i32 new_fd;
  struct pollfd pollable_fds[NET_SERVER_MAX_CLIENTS+1] = {0}; // +1 for the listener socket
  // poll the `listen()`ed socket_fd
  pollable_fds[0].fd = server->tcp_server.socket_fd;
  pollable_fds[0].events = POLLIN;
  // poll the udp socket_fd also
  pollable_fds[1].fd = server->udp_server.server_socket;
  pollable_fds[1].events = POLLIN;
  u32 pollable_fd_count = 2;

  i32 poll_event_count;
  while (*should_quit == false) {
    poll_event_count = poll(pollable_fds, pollable_fd_count, 3000); // times out after 3 seconds so that quitting the server will actually quit the server process in relatively short order.
    if (poll_event_count == -1) {
      // TODO handle error better
      *should_quit = true;
      continue;
    }
    for(u32 i = 0; i < pollable_fd_count; i++) {
      bool is_fd_readable = pollable_fds[i].revents & (POLLIN | POLLHUP);
      if (is_fd_readable) {
        printf("readable fd=%d\n", pollable_fds[i].fd);
        bool is_fd_tcp_server_listener = pollable_fds[i].fd == server->tcp_server.socket_fd;
        bool is_fd_udp_server_listener = pollable_fds[i].fd == server->udp_server.server_socket;
        if (is_fd_tcp_server_listener) { // it's a new connection
          printf("reading a tcp connection\n");
          new_fd = accept(server->tcp_server.socket_fd, (struct sockaddr *)&client_address, (socklen_t*)&addrlen);
          // TODO do something with client_address
          if (new_fd == -1) {
            if (addSystemMessage != NULL) {
              addSystemMessage((u8*)"TODO: handle this accept() error for real, bitch");
            }
          } else {
            if (pollable_fd_count < NET_SERVER_MAX_CLIENTS+1) {
              pollable_fds[pollable_fd_count].fd = new_fd;
              pollable_fds[pollable_fd_count].events = POLLIN | POLLHUP;
              pollable_fds[pollable_fd_count].revents = 0;
              pollable_fd_count++;
              if (addSystemMessage != NULL) {
                char sbuf[128] = {0};
                sprintf(sbuf, "new connection on socket=%d, total=%d\n", new_fd, pollable_fd_count);
                addSystemMessage((u8*)sbuf);
              }
            } else {
              send(new_fd, "server full", 11, 0); // go away sir, we are out of space to keep track of this socket
              close(new_fd);
            }
          }
        } else if (is_fd_udp_server_listener) {
          printf("reading a udp message\n");
          bytes_recieved = recvfrom(
            server->udp_server.server_socket,
            message_buffer,
            UDP_MAX_MESSAGE_LEN,
            0,
            (struct sockaddr *)&client_address,
            (socklen_t*)&addrlen
          );
          handleMessage(message_buffer, bytes_recieved, client_address, server->udp_server.server_socket);
          MemoryZero(message_buffer, UDP_MAX_MESSAGE_LEN);
        } else {// Otherwise we're just a regular client
          printf("reading a tcp message\n");
          bytes_recieved = netRecvMessage(pollable_fds[i].fd, message_buffer, addSystemMessage);
          if (bytes_recieved <= 0) { // error condition
            bool client_hung_up = bytes_recieved == 0;
            // TODO do something with the error case
            closeConnection(pollable_fds[i].fd);
            close(pollable_fds[i].fd);
            if (addSystemMessage != NULL) {
              char sbuf[256] = {0};
              sprintf(sbuf, "closed connection on socket=%d, client_hung_up? %s\n", pollable_fds[i].fd, client_hung_up ? "yes" : "no");
              addSystemMessage((u8*)sbuf);
            }
            // copy the last one over the current one and "forget" the last one by decrementing the count
            pollable_fds[i] = pollable_fds[--pollable_fd_count];
          } else { // we got an actual message from this guy
            handleMessage(message_buffer, bytes_recieved, client_address, pollable_fds[i].fd);
          }
          MemoryZero(message_buffer, UDP_MAX_MESSAGE_LEN);
        }
      }
    }
  }
}

// will return only when either should_quit is true or we got a connection error on the tcp client
void netInfiniteReadMultiClient(
  MultiClient* client,
  bool* should_quit,
  HandleMessageCb handleMessage,
  void (*addSystemMessage)(u8* msg)
) {
  fd_set master;    // master file descriptor list
  fd_set read_fds;  // temp file descriptor list for select()
  i32 fdmax;        // maximum file descriptor number
  FD_ZERO(&master);    // clear the master and temp sets
  FD_ZERO(&read_fds);
  // add both client sockets to the master set
  FD_SET(client->udp_client.socket, &master);
  FD_SET(client->tcp_client.socket, &master);
  // keep track of the biggest file descriptor
  fdmax = Max(client->udp_client.socket, client->tcp_client.socket); // so far, it's this one

  u8 message_buffer[UDP_MAX_MESSAGE_LEN] = {0};
  i32 bytes_recieved = 0;
  SocketAddress client_address = {0};
  i32 addrlen = sizeof(struct sockaddr);
  bool got_connection_error = false;
  while (*should_quit == false && got_connection_error == false) {
    read_fds = master; // copy it
    if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
      perror("select");
      got_connection_error = true;
      client->tcp_client.ready = false;
      close(client->tcp_client.socket);
      return;
    }

    for(i32 i = 0; i <= fdmax; i++) {
      if (FD_ISSET(i, &read_fds)) {
        bool is_tcp_socket = i == client->tcp_client.socket;
        if (is_tcp_socket) {
          bytes_recieved = netRecvMessage(client->tcp_client.socket, message_buffer, addSystemMessage);
          if (bytes_recieved == -1) {
            if (addSystemMessage != NULL) {
              char sbuf[128] = {0};
              sprintf(sbuf, "TODO handle this error, bytes_recieved=%d\n", bytes_recieved);
              addSystemMessage((u8*)sbuf);
            }
            got_connection_error = true;
            client->tcp_client.ready = false;
            close(client->tcp_client.socket);
            continue;
          } else if (bytes_recieved == 0) { // the server hung up
            if (addSystemMessage != NULL) {
              char sbuf[128] = {0};
              sprintf(sbuf, "the server seems to have hung up on us, bytes_recieved=%d\n", bytes_recieved);
              addSystemMessage((u8*)sbuf);
            }
            got_connection_error = true;
            client->tcp_client.ready = false;
            close(client->tcp_client.socket);
            continue;
          }
          handleMessage(message_buffer, bytes_recieved, client->tcp_client.server_address, client->tcp_client.socket);
          MemoryZero(message_buffer, UDP_MAX_MESSAGE_LEN);
        } else { // UDP
          bytes_recieved = recvfrom(client->udp_client.socket, message_buffer, UDP_MAX_MESSAGE_LEN, 0, (struct sockaddr *)&client_address, (socklen_t*)&addrlen);
          handleMessage(message_buffer, bytes_recieved, client_address, client->udp_client.socket);
          MemoryZero(message_buffer, UDP_MAX_MESSAGE_LEN);
        }
      }
    }
  }
}

i32 sendallto(i32 socket, void* buf, i32 len, SocketAddress* to) {
  u32 total_sent = 0;
  i32 left_to_send = len;
  i32 sent_this_round;
  while(total_sent < len) {
    sent_this_round = sendto(
      socket,
      buf+total_sent,
      left_to_send,
      0,
      (const struct sockaddr *)to,
      sizeof(struct sockaddr)
    );
    if (sent_this_round == -1) { return -1; }
    total_sent += sent_this_round;
    left_to_send -= sent_this_round;
  }
  return total_sent;
}

i32 sendUDPu8List(i32 using_socket, SocketAddress* to, u8List* message) {
  return sendallto(using_socket, message->items, message->length, to);
}

i32 sendUDPMessage(UDPServer* to, u8* message, u32 len) {
  return sendallto(to->server_socket, message, len, &to->server_address);
}

i32 sendall(i32 socket, void* buf, i32 len) {
  u32 total_sent = 0;
  i32 left_to_send = len;
  i32 sent_this_round;
  while(total_sent < len) {
    sent_this_round = send(socket, buf+total_sent, left_to_send, 0);
    if (sent_this_round == -1) { return -1; }
    total_sent += sent_this_round;
    left_to_send -= sent_this_round;
  }
  return total_sent;
}

i32 sendTCPMessage(NetworkMessage msg) {
  assert(msg.tcp);
  assert(msg.socket_fd >= 0);
  u16 msg_len = htons(msg.bytes_len);
  i32 result = sendall(msg.socket_fd, (void*)&msg_len, 2);
  if (result == -1) {
    return result;
  }
  return sendall(msg.socket_fd, msg.bytes, msg.bytes_len);
}


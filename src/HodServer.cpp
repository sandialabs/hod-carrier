// Copyright 2022 National Technology & Engineering Solutions of Sandia, LLC 
// (NTESS).  Under the terms of Contract DE-NA0003525 with NTESS, the U.S. 
// Government retains certain rights in this software.

#include "HodServer.hpp"
#include <atomic>
#include <future>
#include <stdio.h>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <netdb.h>
#include <assert.h>

#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define DEBUG_CONNECTION

#ifdef DEBUG_CONNECTION
#define dbg_connection(...) {printf("[SERVER] "); printf(__VA_ARGS__); fflush(stdout);}
#define dbg_connection_raw(...) {printf(__VA_ARGS__); fflush(stdout);}
#else
#define dbg_connection(...) 
#define dbg_connection_raw(...)
#endif

// In some cases, we want to use variables in debug messages that aren't otherwise used.
// This macro can be used to silence the compiler warnings associated with variables used this way
#ifdef DEBUG_CONNECTION
#define dbg_unused(v)
#else
#define dbg_unused(v) (void)v
#endif


Server::Server() : completed_transfers(), completed_transfers_lock() 
{ };

int Server::init(std::string addr_str)
{
  int rc     = 0;

  char *device_name = getenv("IBVERBS_SERVER_DEVICE");
  if( device_name != nullptr ) {
    dbg_connection("LOOKING for DEVICE --> %s\n", device_name);
  } else {
    printf("DEVICE name not specified\n");
    fflush(stdout);
    return -1;
  }

  rc = ibv.open_device(device_name);
  if (rc) {
    printf("[ibverbs_transport] open_device FAILED");
    return -1;
  }

  rc = ibv.setup_command_channel();
  if (rc) {
    printf("[ibverbs_transport] setup_command_channel FAILED");
    return -1;
  }

  rc = ibv.setup_rdma_channel();
  if (rc) {
    printf("[ibverbs_transport] setup_rdma_channel FAILED");
    return -1;
  }

  rc = ibv.create_qps();
  if (rc) {
    printf("[ibverbs_transport] create_qps FAILED");
    return -1;
  }

  dbg_connection("InfiniBand (ibverbs) INITIALIZED\n");

  // ------ SOCKET code -------
  int n;
  char *portno_string = getenv("IBVERBS_CLIENT_SOCKET_PORT");
  if( portno_string == nullptr ) {
    printf("ERROR: Client port (IBVERBS_CLIENT_SOCKET_PORT) not set\n");
    return -1;
  }
  int portno = atoi(portno_string);
  struct sockaddr_in client_addr;
  char buffer[256];

  /* SERVER code */
  client_sockfd = socket(AF_INET, SOCK_STREAM, 0);
  client_addr.sin_addr.s_addr = inet_addr(addr_str.c_str());
  client_addr.sin_family = AF_INET;
  client_addr.sin_port = htons(portno);
  dbg_connection("CONNECTing to %s / %d\n", addr_str.c_str(), portno);
  dbg_connection("CONNECTing to %s / %d\n", inet_ntoa(client_addr.sin_addr), portno);
  int number_of_attempts = 5;
  for( int i = 0; i < number_of_attempts; i++ ) {
    rc = connect(client_sockfd,(struct sockaddr *)&client_addr,sizeof(client_addr));
    if( rc == 0 ) {
      // we did it
      break;
    }
    sleep(1);
  }

  if( rc < 0 ) {
    printf("UNABLE to CONNECT (%d attempts / rc = %d / %s : errno = %d)\n", 
           number_of_attempts, rc, strerror(errno), errno);
    return -1;
  }

  /* RECEIVE RDMA qpn */
  memset(buffer, 0, 256);
  n = read(client_sockfd, buffer, 255);
  dbg_unused(n);
  dbg_connection("RECEIVE %d bytes (%s)\n", n, strerror(errno));
  dbg_connection("RECEIVE RDMA qpn = %s\n", buffer);
  ibv.set_peer_rdma_qpn(atoi(buffer));

  /* SEND RDMA qpn */
  memset(buffer, 0, 256);
  sprintf(buffer, "%d", ibv.get_rdma_qpn()); 
  dbg_connection("SEND RDMA qpn = %s\n", buffer);
  n = write(client_sockfd, buffer, strlen(buffer));
  dbg_unused(n);
  dbg_connection("SEND %d bytes\n", n);

  /* RECEIVE CMD qpn */
  memset(buffer, 0, 256);
  n = read(client_sockfd, buffer, 255);
  dbg_unused(n);
  dbg_connection("RECEIVE %d bytes (%s)\n", n, strerror(errno));
  dbg_connection("RECEIVE RDMA qpn = %s\n", buffer);
  ibv.set_peer_cmd_qpn(atoi(buffer));

  /* SEND CMD qpn */
  memset(buffer, 0, 256);
  sprintf(buffer, "%d", ibv.get_cmd_qpn()); 
  dbg_connection("SEND RDMA qpn = %s\n", buffer);
  n = write(client_sockfd, buffer, strlen(buffer));
  dbg_connection("SEND %d bytes\n", n);

  /* RECEIVE lid */
  memset(buffer, 0, 256);
  n = read(client_sockfd, buffer, 255);
  dbg_unused(n);
  dbg_connection("RECEIVE %d bytes (%s)\n", n, strerror(errno));
  dbg_connection("RECEIVE RDMA lid = %s\n", buffer);
  ibv.set_peer_lid(atoi(buffer));

  /* SEND lid */
  memset(buffer, 0, 256);
  sprintf(buffer, "%d", ibv.get_lid()); 
  dbg_connection("SEND RDMA lid = %s\n", buffer);
  n = write(client_sockfd, buffer, strlen(buffer));
  dbg_unused(n);
  dbg_connection("SEND %d bytes\n", n);

  rc = ibv.transition_to_ready();
  if( rc < 0 ) {
    printf("FAILED to transition to READY\n");
    fflush(stdout);
    return -1;
  }

  dbg_connection("Infiniband READY\n");

  return 0;
}

uint64_t Server::get_remote_buffer(uint64_t remote_buffer_addr, size_t remote_buffer_size,
                                   uint32_t remote_buffer_key)
{
  ibv.get_remote_buffer(remote_buffer_addr, remote_buffer_size, remote_buffer_key);
  completed_transfers_lock.lock();
  completed_transfers.push(remote_buffer_addr);
  completed_transfers_lock.unlock();
  return remote_buffer_addr;
}

#define BUFFER_SIZE 256
int Server::start()
{
  std::vector<std::future<uint64_t>> futures;
  uint64_t remote_buffer_addr = (uint64_t)nullptr;
  size_t remote_buffer_size = 0;
  uint32_t remote_buffer_key = 0;
  char buffer[BUFFER_SIZE];
  int n;

  while(1) {
    /* RECEIVE remote buffer addr --OR-- COMPLETE message */
    memset(buffer, 0, 256);
    n = read(client_sockfd, buffer, 255);
    dbg_connection("RECEIVED %d BYTES\n", n);
    if( strncmp(buffer, (char *)"COMPLETE", 256) == 0 ) {
      dbg_connection("RECEIVED ACK --> wait for FUTUREs\n");

      // MAKE sure all of the threads have completed
      std::vector<std::future<uint64_t>>::iterator it;
      for( it = futures.begin(); it != futures.end(); ++it ) {
        (*it).wait();
      }
      futures.clear();
      dbg_connection("FUTUREs complete\n");

      /* SEND ACK complete */
      memset(buffer, 0, 256);
      sprintf(buffer, "COMPLETE");
      dbg_connection("SEND ACK COMPLETE\n");
      n = write(client_sockfd, buffer, strlen(buffer));
      dbg_unused(n);
      dbg_connection("SEND %d bytes\n", n);

      dbg_connection("CLOSING CLIENT socket (COMPLETE)\n");
      if( close(client_sockfd) != 0 ) {
        printf("ERROR: Failed to CLOSE client socket");
        fflush(stdout);
        return -1;
      }
      dbg_connection("CLIENT socket closed SUCCESSFULLY\n");
      client_sockfd = -1;
      return 0;
    } else if( n <= 0 ) {
      dbg_connection("CLIENT socket closed unexpectedly (%s)\n", strerror(errno));
      fflush(stdout);
      return -1;
    }

    dbg_unused(n);
    dbg_connection("RECEIVE %d bytes\n", n);
    dbg_connection("RECEIVE RDMA remote buffer = %s\n", buffer);

    char *string_token;

    string_token = strtok(buffer,":");
    remote_buffer_addr = strtoul(string_token, NULL, 16);
    string_token = strtok(NULL,":");
    remote_buffer_size = strtoul(string_token, NULL, 16);
    string_token = strtok(NULL,":");
    remote_buffer_key = strtoul(string_token, NULL, 16);
    
    /* SEND ACK */
    memset(buffer, 0, 256);
    char *p = buffer;
    int n;
    n = sprintf(buffer, "ACK");

    completed_transfers_lock.lock();
    while( !completed_transfers.empty() ) {
      p += n;
      uint64_t addr = completed_transfers.front();
      completed_transfers.pop();
      n = sprintf(p,":0x%lx", addr);
    }
    completed_transfers_lock.unlock();

    dbg_connection("SEND ACK RDMA remote buffer addr = %s\n", buffer);
    n = write(client_sockfd, buffer, strlen(buffer));
    dbg_unused(n);
    dbg_connection("SEND %d bytes\n", n);

    std::future<uint64_t> future = std::async(&Server::get_remote_buffer, this, 
                                              remote_buffer_addr,
                                              remote_buffer_size, 
                                              remote_buffer_key);
    futures.push_back(move(future));
  }
  return 0;
}

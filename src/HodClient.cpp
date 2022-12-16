// Copyright 2022 National Technology & Engineering Solutions of Sandia, LLC 
// (NTESS).  Under the terms of Contract DE-NA0003525 with NTESS, the U.S. 
// Government retains certain rights in this software.

#include "HodClient.hpp"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <netdb.h>

#include <fcntl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

#define DEBUG_CONNECTION

#ifdef DEBUG_CONNECTION
#define dbg_connection(...) {printf("[CLIENT] "); printf(__VA_ARGS__); fflush(stdout);}
#else
#define dbg_connection(...) 
#endif

// In some cases, we want to use variables in debug messages that aren't otherwise used.
// This macro can be used to silence the compiler warnings associated with variables used this way
#ifdef DEBUG_CONNECTION
#define dbg_unused(v)
#else
#define dbg_unused(v) (void)v
#endif

int Client::init()
{
  int rc     = 0;

  dbg_connection("[ibverbs_transport] ENTER\n");

  /* INITIALIZE socket */
  struct sockaddr_in server_addr, host_addr;
  char *portno_string = getenv("IBVERBS_CLIENT_SOCKET_PORT");
  if( portno_string == nullptr ) { 
    printf("ERROR: Client port (IBVERBS_CLIENT_SOCKET_PORT) not set\n");
    return -1; 
  }
  int portno = atoi(portno_string);

  host_sockfd = socket(AF_INET, SOCK_STREAM, 0);

  /* Get the IPv4 address of the socket network interface */
  struct ifreq ifr;
  ifr.ifr_addr.sa_family = AF_INET;
  char *client_socket_if = getenv("IBVERBS_CLIENT_SOCKET_IF");
  if( client_socket_if == nullptr ) {
    printf("[CLIENT][ibverbs_connection] IBV_CLIENT_SOCKET_IF not found\n");
    return -1;
  }
  strncpy(ifr.ifr_name, client_socket_if, IFNAMSIZ-1);
  ioctl(host_sockfd, SIOCGIFADDR, &ifr);
  char *addr_str = inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
  dbg_connection("FOUND address for: %s / %s\n", ifr.ifr_name, addr_str);

  host_addr.sin_addr = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
  host_addr.sin_family = AF_INET;
  host_addr.sin_port = htons(portno);
  dbg_connection("BINDing to %s / %d\n", inet_ntoa(host_addr.sin_addr), portno);
  rc = bind(host_sockfd, (struct sockaddr *) &host_addr, sizeof(host_addr));
  if( rc < 0 ) {
    printf("[CLIENT] Failed to BIND to %s / %d (rc = %d / %s)\n",
           addr_str, portno, rc, strerror(errno));
    return -1;
  }
  dbg_connection("LISTENING to %s / %d\n", inet_ntoa(host_addr.sin_addr), portno);
  rc = listen(host_sockfd, 5);
  if( rc < 0 ) {
    printf("[CLIENT] LISTEN failed on %s / %d (rc = %d / %s)\n",
           inet_ntoa(host_addr.sin_addr), portno, rc, strerror(errno));
    return -1;
  }

  dbg_connection("[ibverbs_transport] initializing InfiniBand\n");

  char *device_name = getenv("IBV_CLIENT_DEVICE");
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

  dbg_connection("[ibverbs_transport] InfiniBand (ibverbs) Initialized\n");

  char buffer[256];
  int n;

  socklen_t length =  sizeof(server_addr);
  server_sockfd = accept(host_sockfd, (struct sockaddr *)&server_addr, &length);
  if( server_sockfd < 0 ) {
    printf("[CLIENT] ACCEPT failed on %s / %d (rc = %d / %s)\n",
           inet_ntoa(host_addr.sin_addr), portno, rc, strerror(errno));
    return -1;
  }
  dbg_connection("CLIENT success (server_sockfd (%p) = %d)!\n", &server_sockfd, server_sockfd);

  /* SEND RDMA qpn */
  memset(buffer, 0, 256);
  sprintf(buffer, "%d", ibv.get_rdma_qpn()); 
  dbg_connection("SEND RDMA qpn = %s\n", buffer);
  n = write(server_sockfd, buffer, strlen(buffer));
  dbg_unused(n);
  dbg_connection("SEND %d bytes\n", n);

  /* RECEIVE RDMA qpn */
  memset(buffer, 0, 256);
  n = read(server_sockfd, buffer, 255);
  dbg_unused(n);
  dbg_connection("RECEIVE %d bytes\n", n);
  dbg_connection("RECEIVE RDMA qpn = %s\n", buffer);
  ibv.set_peer_rdma_qpn(atoi(buffer));

  /* SEND CMD qpn */
  memset(buffer, 0, 256);
  sprintf(buffer, "%d", ibv.get_cmd_qpn()); 
  printf("SEND CMD qpn = %s\n", buffer);
  n = write(server_sockfd, buffer, strlen(buffer));
  dbg_unused(n);
  dbg_connection("SEND %d bytes\n", n);

  /* RECEIVE CMD qpn */
  memset(buffer, 0, 256);
  n = read(server_sockfd, buffer, 255);
  dbg_unused(n);
  dbg_connection("RECEIVE %d bytes\n", n);
  dbg_connection("RECEIVE CMD qpn = %s\n", buffer);
  ibv.set_peer_cmd_qpn(atoi(buffer));

  /* SEND lid */
  memset(buffer, 0, 256);
  sprintf(buffer, "%d", ibv.get_lid()); 
  dbg_connection("SEND RDMA lid = %s\n", buffer);
  n = write(server_sockfd, buffer, strlen(buffer));
  dbg_unused(n);
  dbg_connection("SEND %d bytes\n", n);

  /* RECEIVE lid */
  memset(buffer, 0, 256);
  n = read(server_sockfd, buffer, 255);
  dbg_unused(n);
  dbg_connection("RECEIVE %d bytes\n", n);
  dbg_connection("RECEIVE RDMA lid = %s\n", buffer);
  ibv.set_peer_lid(atoi(buffer));

  if( ibv.transition_to_ready() < 0 ) return -1;
  dbg_connection("[ibverbs_transport] READY\n");
  dbg_connection("CLIENT init complete (server_sockfd (%p) = %d)!\n", &server_sockfd, server_sockfd);

  return 0;
}

#define BUFFER_SIZE 256
int Client::run(char *rdma_buffer, size_t rdma_buffer_size)
{
  int rc;
  dbg_connection("==== RUN ====\n");
  dbg_connection("rdma_buffer = %p / rdma_buffer_size = %ld\n", rdma_buffer, rdma_buffer_size);

  /* SEND MR */
  struct ibv_mr *local_ibv_mr = ibv.register_memory(rdma_buffer, rdma_buffer_size);

  // ------ SOCKET code -------
  int n;
  char buffer[BUFFER_SIZE];

  dbg_connection("(RDMA buffer = %p / local_ibv_mr = 0x%x)\n", 
                 rdma_buffer, local_ibv_mr->rkey);
  dbg_connection("(RDMA buffer = 0x%x 0x%x 0x%x\n", 
                 rdma_buffer[0], rdma_buffer[1], rdma_buffer[2]);

  /* ---- */
  struct ibv_sge     sge;
  struct ibv_recv_wr recv_wr;

  memset(&sge, 0, sizeof(sge));
  memset(&recv_wr, 0, sizeof(recv_wr));
  sge.addr   = (uint64_t)rdma_buffer;
  sge.length = rdma_buffer_size;
  sge.lkey   = local_ibv_mr->lkey;

  recv_wr.next    = nullptr;
  recv_wr.sg_list = &sge;
  recv_wr.num_sge = 1;
  recv_wr.wr_id = (uint64_t)0xBADFACE;

  dbg_connection("post_recv() - cmd_msg=%p - sge.addr=%lx ; sge.length=%u ; sge.lkey=%x\n",
                 rdma_buffer, sge.addr, sge.length, sge.lkey);

  rc = ibv.post_recv_wr(&recv_wr);
  if( rc < 0 ) {
    printf("post_recv() FAILED\n");
    return -1;
  } 
  
  /* ---- */

  /* SEND rdma_buffer, buffer size, and rkey */
  memset(buffer, 0, BUFFER_SIZE);
  sprintf(buffer, "0x%lx:0x%lx:0x%x", (uint64_t)local_ibv_mr->addr, 
          (uint64_t)rdma_buffer_size, local_ibv_mr->rkey); 

  n = write(server_sockfd, buffer, strlen(buffer));
  if( n < 0 ) {
    printf("[ibverbs_transport] ERROR: %d(%s)\n", errno, strerror(errno));
    return -1;
  }
  dbg_unused(n);
  dbg_connection("SEND %d bytes\n", n);
  dbg_connection("SEND buffer = %s\n", buffer);

  /* RECEIVE ACK */
  memset(buffer, 0, 256);
  n = read(server_sockfd, buffer, 255);
  dbg_unused(n);
  dbg_connection("RECEIVE %d bytes\n", n);
  dbg_connection("RECEIVE ACK = %s\n", buffer);

  /* CLEAN UP completed transfers. */
  char *memory;
  char *element = strtok(buffer,":");
  while( 1 ) {
    element = strtok(NULL,":");
    if( element == NULL ) {
      break;
    }
    memory = (char *)strtol(element, NULL, 0);
    if( pending_transfers.size() > 1 ) {
        printf("**** WHOOPS : Number of PENDING CHECKPOINTS = %ld\n", pending_transfers.size());
    }
    while( !pending_transfers.empty() ) {
      std::pair<char *,struct ibv_mr *> next = pending_transfers.front();
      pending_transfers.pop();
      if( next.first != memory ) {
        printf("**ERROR** : out-of-order ACKNOWLEDGEMENT (ACKed %p / FOUND %p)\n", 
               memory, next.first);
      } else {
        dbg_connection("FOUND matching ADDRESS (FREE %p)\n", memory);
        free(next.first);
        ibv_dereg_mr(next.second);
        break;
      }
    }
  }
  
  pending_transfers.push(std::make_pair(rdma_buffer,local_ibv_mr));
  return 0;
}

int Client::stop()
{
  /* SEND complete message */
  char buffer[256];
  memset(buffer, 0, 256);
  sprintf(buffer, "%s", "COMPLETE");
  dbg_connection("SENDING complete message\n");
  int n = write(server_sockfd, buffer, strlen(buffer));
  dbg_unused(n);
  dbg_connection("SENT %d bytes\n", n); 

  /* RECEIVE ACK */
  memset(buffer, 0, 256);
  n = read(server_sockfd, buffer, 255);
  dbg_unused(n);
  dbg_connection("RECEIVE %d bytes\n", n);
  dbg_connection("RECEIVE ACK COMPLETE = %s\n", buffer);
  if( strcmp("COMPLETE", buffer) != 0 ) {
    printf("*** ERROR: COMPLETE ack not received\n");
    fflush(stdout);
  }

  if( close(server_sockfd) != 0 ) {
    printf("ERROR: FAILED to close SERVER socket\n");
    return -1;
  } else {
    dbg_connection("SUCCESSFULLY closed SERVER socket\n"); 
    server_sockfd = -1;
  }
  if( close(host_sockfd) != 0 ) {
    printf("ERROR: FAILED to close HOST socket\n");
    return -1;
  } else {
    dbg_connection("SUCCESSFULLY closed HOST socket\n"); 
    host_sockfd = -1;
  }

  return 0;
}

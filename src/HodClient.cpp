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

#define BUFFER_SIZE 1024

//#define DEBUG_CONNECTION

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

Client::Client() : ibv(), pending_transfers(), registered_memory() 
{ };

int Client::init(std::string init_msg)
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

  rc = ibv.create_qps();
  if (rc) {
    printf("[ibverbs_transport] create_qps FAILED");
    return -1;
  }

  dbg_connection("[ibverbs_transport] InfiniBand (ibverbs) Initialized\n");

  char buffer[BUFFER_SIZE];
  int n;

  socklen_t length =  sizeof(server_addr);
  server_sockfd = accept(host_sockfd, (struct sockaddr *)&server_addr, &length);
  if( server_sockfd < 0 ) {
    printf("[CLIENT] ACCEPT failed on %s / %d (rc = %d / %s)\n",
           inet_ntoa(host_addr.sin_addr), portno, rc, strerror(errno));
    return -1;
  }
  dbg_connection("CLIENT success (server_sockfd (%p) = %d)!\n", &server_sockfd, server_sockfd);

  /* SEND CMD qpn, lid, and init_msg */
  memset(buffer, 0, BUFFER_SIZE);
  sprintf(buffer, "%d:%d:%sfoo", ibv.get_cmd_qpn(), ibv.get_lid(), init_msg.c_str()); 
  n = write(server_sockfd, buffer, strlen(buffer));
  dbg_unused(n);
  dbg_connection("SEND %d bytes\n", n);

  /* RECEIVE CMD qpn, lid */
  memset(buffer, 0, BUFFER_SIZE);
  n = read(server_sockfd, buffer, BUFFER_SIZE-1);

  char *element = strtok(buffer,":");
  ibv.set_peer_cmd_qpn(atoi(element));

  element = strtok(NULL,":");
  ibv.set_peer_lid(atoi(element));

  if( ibv.transition_to_ready() < 0 ) return -1;
  dbg_connection("[ibverbs_transport] READY\n");
  dbg_connection("CLIENT init complete (server_sockfd (%p) = %d)!\n", &server_sockfd, server_sockfd);

  return 0;
}

int Client::ready()
{
  char buffer[BUFFER_SIZE];
  memset(buffer, 0, BUFFER_SIZE);
  sprintf(buffer, "READY");
  write(server_sockfd, buffer, strlen(buffer));

  char server_buffer[BUFFER_SIZE];
  memset(server_buffer, 0, BUFFER_SIZE);
  read(server_sockfd, server_buffer, BUFFER_SIZE-1);
  if( strcmp(server_buffer, "READY") != 0 ) {
    printf("(Client) READY failed (%s)\n", server_buffer);
    fflush(stdout);
    abort();
  }

  return 0;
}

struct ibv_mr *Client::register_memory(unsigned char *rdma_buffer, size_t rdma_buffer_size)
{
  struct ibv_mr *local_ibv_mr = ibv.register_memory(rdma_buffer, rdma_buffer_size, IBV_ACCESS_REMOTE_READ);
  return local_ibv_mr;
}

void Client::deregister_memory(struct ibv_mr *registered_rdma_buffer)
{
  ibv.deregister_memory(registered_rdma_buffer);
}
  
int Client::request_transfer(struct ibv_mr *local_ibv_mr, size_t rdma_buffer_size,
                             std::function<void(struct ibv_mr *)> callback)
{
  int rc;
  std::list<std::pair< struct ibv_mr *,size_t > > buffer_list;

  buffer_list.push_back(std::make_pair(local_ibv_mr, rdma_buffer_size));
  rc = request_transfer(buffer_list, callback);
  return rc;
}

int Client::request_transfer(std::list<std::pair< struct ibv_mr *,size_t > > &buffer_list,
                             std::function<void(struct ibv_mr *)> callback)
{
  dbg_connection("==== REQUEST TRANSFER ====\n");

  // ------ SOCKET code -------
  int n;
  std::list<std::pair< struct ibv_mr *,size_t > >::iterator it;

  for( it = buffer_list.begin(); it != buffer_list.end(); ++it ) {
    dbg_connection("(RDMA buffer = %p (%ld bytes) / rkey = 0x%x)\n", 
                   (it->first)->addr, (it->first)->length, (it->first)->rkey);
  }

  /* SEND rdma_buffer, buffer size, and rkey */

  // the SPECIFICATION of EACH memory region requires up to 49 characters
  size_t request_buffer_size = buffer_list.size()*49*sizeof(char)+1;
  char *request_buffer = (char *)malloc(request_buffer_size);

  char *write_ptr = request_buffer;
  for( it = buffer_list.begin(); it != buffer_list.end(); ++it ) {
    n = sprintf(write_ptr, "0x%lx:0x%lx:0x%x,", (uint64_t)(it->first)->addr, 
                (uint64_t)it->second, (it->first)->rkey); 
    write_ptr += n;
    pending_transfers[(it->first)->addr] = std::make_tuple((it->first), callback);
  }

  n = write(server_sockfd, request_buffer, strlen(request_buffer));
  if( n < 0 ) {
    printf("[ibverbs_transport] ERROR: %d(%s)\n", errno, strerror(errno));
    return -1;
  }
  dbg_unused(n);
  dbg_connection("SEND %d bytes\n", n);
  dbg_connection("SEND buffer = %s\n", request_buffer);

  /* RECEIVE ACK */
  char ack_buffer[1024];
  memset(ack_buffer, 0, 1024);
  n = read(server_sockfd, ack_buffer, 1023);
  dbg_unused(n);
  dbg_connection("RECEIVE %d bytes\n", n);
  dbg_connection("RECEIVE ACK = %s\n", ack_buffer);

  /* CLEAN UP completed transfers. */
  process_completed_buffer_list(ack_buffer);
   
  free(request_buffer);
  return 0;
}

int Client::stop()
{
  /* SEND complete message */
  char buffer[1024];
  memset(buffer, 0, 256);
  sprintf(buffer, "%s", "COMPLETE");
  dbg_connection("SENDING complete message\n");
  int n = write(server_sockfd, buffer, strlen(buffer));
  dbg_unused(n);
  dbg_connection("SENT %d bytes\n", n); 

  /* RECEIVE ACK */
  memset(buffer, 0, 1024);
  n = read(server_sockfd, buffer, 1023);
  dbg_unused(n);
  dbg_connection("RECEIVE %d bytes\n", n);
  dbg_connection("RECEIVE ACK COMPLETE = %s\n", buffer);
  process_completed_buffer_list(buffer);

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

// EXECUTE the callback for all of the buffers that have been transferred.
void Client::process_completed_buffer_list(char *buffer_list)
{
  char *element = strtok(buffer_list, ":");

  // Skip the preamble (e.g., "ACK" or "COMPLETE") text
  element = strtok(NULL,":");

  while( element != nullptr ) {
    // GET address of buffer that completed
    void *memory = (void *)strtol(element, NULL, 0);

    std::map< void *, std::tuple< struct ibv_mr *, std::function<void(struct ibv_mr *)> > >::iterator map_iter;

    map_iter = pending_transfers.find(memory);
    if( map_iter != pending_transfers.end() ) {

      // GET the value (memory descriptor + callback) associated with the completed buffer
      std::tuple< struct ibv_mr *, std::function<void(struct ibv_mr *)> > next = 
        (*map_iter).second;

      // GET the memory descriptor and callback for the completed buffer
      struct ibv_mr *mr = std::get<0>(next);
      std::function<void(struct ibv_mr *)> callback = std::get<1>(next);
      callback(mr);
      pending_transfers.erase(map_iter);

    } else {
      printf("ERROR: unable to LOCATE completed buffer @ %p (pending_transfers = %ld)\n", 
             memory, pending_transfers.size());
    }
  
    element = strtok(NULL,":");
  }
}

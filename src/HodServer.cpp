// Copyright 2022 National Technology & Engineering Solutions of Sandia, LLC 
// (NTESS).  Under the terms of Contract DE-NA0003525 with NTESS, the U.S. 
// Government retains certain rights in this software.

#include "HodServer.hpp"
#include <atomic>
#include <future>
#include <chrono>
#include <list>
#include <thread> // this_thread
#include <iostream> // cout
#include <numeric>  // std::accumulate
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

#define BUFFER_SIZE 1024
//#define DEBUG_CONNECTION
#define TIME_MEASUREMENT

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

Server::Server() : idle_registered_memory(), active_registered_memory(), threading_enabled(true)
{ 
};

Server::~Server() 
{ 
  // NOTE: shouldn't need a lock here b/c it's the DTOR
  std::list<struct ibv_mr *>::iterator it;

  for( it = idle_registered_memory.begin(); it != idle_registered_memory.end(); ++it ) {
    cleanup_callback(*it);
  }
  idle_registered_memory.clear();

  // Are there any memory buffers that are still "ACTIVE"?
  if( active_registered_memory.size() != 0 ) {
    printf("SHUTTING down while memory buffers are still active\n.");
    fflush(stdout);
  }
};

// <init_msg> is a string that allows the CLIENT to encode information about the nature of the
// data that it intends to send (e.g., the type and structure of the data that will be transferred).
int Server::init(std::string addr_str, std::string *init_msg)
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
  char buffer[BUFFER_SIZE];

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

  /* RECEIVE CMD qpn, lid, and init_msg */
  memset(buffer, 0, BUFFER_SIZE);
  n = read(client_sockfd, buffer, BUFFER_SIZE-1);
  dbg_unused(n);
  dbg_connection("RECEIVE %d bytes (%s)\n", n, strerror(errno));

  char *element = strtok(buffer,":");
  ibv.set_peer_cmd_qpn(atoi(element));

  element = strtok(NULL,":");
  ibv.set_peer_lid(atoi(element));

  if( init_msg ) {
    element = strtok(NULL,":");
    // CALCULATE the number of bytes in the init_string
    size_t size = n - (element-buffer);
    init_msg->assign(element, size);
  }

  /* SEND CMD qpn, lid */
  sprintf(buffer, "%d:%d", ibv.get_cmd_qpn(), ibv.get_lid());
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

int Server::ready()
{
  char client_buffer[BUFFER_SIZE];
  memset(client_buffer, 0, BUFFER_SIZE);
  int n = read(client_sockfd, client_buffer, BUFFER_SIZE-1);
  if( strcmp(client_buffer, "READY") != 0 ) {
    printf("(Server) READY failed (%s)\n", client_buffer);
    fflush(stdout);
    abort();
  }

  char buffer[BUFFER_SIZE];
  memset(buffer, 0, BUFFER_SIZE);
  sprintf(buffer, "READY");
  n = write(client_sockfd, buffer, strlen(buffer));

  return 0;
}

// REGISTER memory for RDMA.
struct ibv_mr *Server::register_memory(unsigned char *rdma_buffer, size_t rdma_buffer_size)
{
  /* SEND MR */
  int access = IBV_ACCESS_LOCAL_WRITE |
               IBV_ACCESS_REMOTE_WRITE |
               IBV_ACCESS_REMOTE_READ |
               IBV_ACCESS_REMOTE_ATOMIC;

  struct ibv_mr *local_ibv_mr = ibv.register_memory(rdma_buffer, rdma_buffer_size, access);
  return local_ibv_mr;
}

// ADD memory for Server to manage.  Allows the user to implicitly convey information about how big message buffers
// should be.
int Server::add_memory(void *rdma_buffer, size_t rdma_buffer_size)
{
  /* SEND MR */
  int access = IBV_ACCESS_LOCAL_WRITE |
               IBV_ACCESS_REMOTE_WRITE |
               IBV_ACCESS_REMOTE_READ |
               IBV_ACCESS_REMOTE_ATOMIC;

  struct ibv_mr *local_ibv_mr = ibv.register_memory(rdma_buffer, rdma_buffer_size, access);
  registered_memory_lists_lock.lock();
  idle_registered_memory.push_back(local_ibv_mr);
  registered_memory_lists_lock.unlock();
  return 0;
}

int Server::remove_memory(unsigned char *registered_rdma_buffer)
{
  registered_memory_lists_lock.lock();
  std::list<struct ibv_mr *>::iterator it;

  for( it = idle_registered_memory.begin(); it != idle_registered_memory.end(); ++it ) {
    if( (*it)->addr == registered_rdma_buffer ) {
      break;
    }
  }

  if( it != idle_registered_memory.end() ) {
    ibv.deregister_memory(*it);
    idle_registered_memory.erase(it);
  } else {
    printf("**** [SERVER deregister_memory] WARNING : buffer address %p NOT FOUND\n", registered_rdma_buffer);
    fflush(stdout);
  }
  registered_memory_lists_lock.unlock();
  return 0;
}

void Server::deregister_memory(struct ibv_mr *registered_rdma_buffer)
{
  ibv.deregister_memory(registered_rdma_buffer);
}

void Server::set_buffer_processing_callback(std::function<void(std::list< std::tuple<void *,size_t,uint32_t> >&) > cb)
{
  processing_callback = cb;
}
  
void Server::set_buffer_allocator_callback(std::function<void(std::list<size_t>, 
                                                              std::list< std::tuple<void *,size_t,uint32_t > >&) > cb)
{
  allocator_callback = cb;
}

void Server::set_buffer_release_callback(std::function<void(std::list< std::tuple<void *,size_t,uint32_t> >&)> cb)
{
  release_callback = cb;
}

void Server::set_buffer_cleanup_callback(std::function<void(struct ibv_mr *)> cb)
{
  cleanup_callback = cb;
}

void Server::acquire_registered_rdma_buffer(size_t buffer_size, std::tuple<void *, size_t, uint32_t >& buffer)
{
  std::list<size_t> buffer_sizes;
  buffer_sizes.push_back(buffer_size);
  std::list< std::tuple<void *, size_t, uint32_t > > buffers;

  acquire_registered_rdma_buffers(buffer_sizes, buffers);
  if( buffer_sizes.size() != buffers.size() ) {
    printf("ERROR : Memory buffer acquisition FAILED! Aborting.\n");
    fflush(stdout);
    abort();
  }
  buffer = buffers.front();
}

void Server::acquire_registered_rdma_buffers(std::list<size_t> buffer_sizes, 
                                             std::list< std::tuple<void *, size_t, uint32_t > >& buffers)
{
  void *memory;
  uint32_t local_key;
  std::list<size_t>::iterator sizes_it;

  // USE server-managed memory
  for( sizes_it = buffer_sizes.begin(); sizes_it != buffer_sizes.end(); ++sizes_it ) {
    std::tuple<void *, size_t, uint32_t > buffer;
    size_t buffer_size = *sizes_it;

    registered_memory_lists_lock.lock();
    std::list<struct ibv_mr *>::iterator it;
    for( it = idle_registered_memory.begin(); it != idle_registered_memory.end(); ++it ) {
      struct ibv_mr *next = *it;
      // Greedily take the first buffer that is big enough
      if( next->length >= (*sizes_it) ) {
        break;
      }
    }
    registered_memory_lists_lock.unlock();
    if( it != idle_registered_memory.end() ) {
      memory = (void *)((*it)->addr);
      local_key = (*it)->lkey;

      // MOVE the buffer from the IDLE list to the ACTIVE list
      active_registered_memory.push_back(*it);
      idle_registered_memory.erase(it);
    } else {
      // CACHE is empty, allocate a new buffer
      long pagesize = sysconf(_SC_PAGE_SIZE);
      posix_memalign((void **)&memory, pagesize, buffer_size);

      struct ibv_mr *local_ibv_mr = ibv.register_memory((unsigned char *)memory, buffer_size, 
                                                        IBV_ACCESS_LOCAL_WRITE);
      rdma_buffers[local_ibv_mr->addr] = local_ibv_mr;
      memory = (void *)(local_ibv_mr->addr);
      local_key = local_ibv_mr->lkey;
      
      registered_memory_lists_lock.lock();
      active_registered_memory.push_back(local_ibv_mr);
      registered_memory_lists_lock.unlock();
    }

    // ADD new buffer to the output
    buffers.push_back(std::make_tuple(memory,buffer_size,local_key));
  }
}

int Server::release_registered_rdma_buffer(std::tuple<void *,size_t,uint32_t>& buffer)
{
  std::list< std::tuple<void *,size_t,uint32_t> > buffers;
  buffers.push_back(buffer);
  return release_registered_rdma_buffers(buffers);
}

int Server::release_registered_rdma_buffers(std::list< std::tuple<void *,size_t,uint32_t> >& buffers)
{
  int rc = 0;
  std::list< std::tuple<void *,size_t,uint32_t> >::iterator buffer_it;
  for( buffer_it = buffers.begin(); buffer_it != buffers.end(); ++buffer_it ) {
    std::list<struct ibv_mr *>::iterator it;
    registered_memory_lists_lock.lock();
    for( it = active_registered_memory.begin(); it != active_registered_memory.end(); ++it ) {
      if( (*it)->addr == std::get<0>(*buffer_it) ) break;
    }
    
    if( it != active_registered_memory.end() ) {
      // MOVE the buffer from the ACTIVE list to the IDLE list
      idle_registered_memory.push_front(*it);
      active_registered_memory.erase(it);
    } else {
      printf("**** [release_registered_rdma_buffer] WARNING : buffer address %p NOT FOUND\n",
             std::get<0>(*buffer_it));
      fflush(stdout);
      rc = -1;
    }
    registered_memory_lists_lock.unlock();
  }
  return rc;
}

std::tuple<size_t,double>  Server::get_remote_buffer(void *remote_buffer, 
                                                     size_t remote_buffer_size, 
                                                     uint32_t remote_buffer_key)
{
  std::list< std::tuple<void *, size_t, uint32_t> > buffers;
  buffers.push_back(std::make_tuple(remote_buffer, remote_buffer_size, remote_buffer_key));
  
  return get_remote_buffers(&buffers);
}

// GET contents of a list of buffers identified by their address (uint64_t), length (size_t), and rkey (uint32_t)
std::tuple<size_t,double> Server::get_remote_buffers(std::list< std::tuple<void *, size_t, uint32_t> > *buffers)
{
  std::list< std::tuple<void *,size_t,uint32_t> > dst_buffers;
  std::tuple<void *,size_t,uint32_t> dst_buffer;
  std::vector< std::future<uint64_t> > futures;
  std::list<size_t> buffer_sizes; 

  std::list< std::tuple<void *,size_t,uint32_t> >::iterator it;
  std::list< std::tuple<void *,size_t,uint32_t> >::iterator dst_buffer_it;

  // GATHER buffer sizes
  for( it = buffers->begin(); it != buffers->end(); ++it ) {
    buffer_sizes.push_back(std::get<1>(*it));
  }

  // ALLOCATE memory for local buffers
  allocator_callback(buffer_sizes, dst_buffers);

  dst_buffer_it = dst_buffers.begin();
#ifdef TIME_MEASUREMENT
  std::chrono::high_resolution_clock::time_point t1,t2;
  std::chrono::duration<double> elapsed;
  t1 = std::chrono::high_resolution_clock::now();
#endif

  for( it = buffers->begin(); it != buffers->end(); ++it ) {
    /* REMOTE buffer */
    void *remote_buffer = std::get<0>(*it);
    size_t buffer_size = std::get<1>(*it);
    uint32_t remote_buffer_key = std::get<2>(*it);

    /* LOCAL buffer */

    /* Underflow should never happen, but check anyway */
    if( dst_buffer_it == dst_buffers.end() ) {
      printf("**** Error: EXHAUSTED local destination buffers\n");
      fflush(stdout);
      abort();
    }
    dst_buffer = *dst_buffer_it++;
    void *local_buffer = std::get<0>(dst_buffer);
    uint32_t local_buffer_key = std::get<2>(dst_buffer);

    if( buffers->size() == 1 || !threading_enabled ) {
      ibv.get_remote_buffer((uint64_t)local_buffer, local_buffer_key, (uint64_t)remote_buffer, 
                            remote_buffer_key, buffer_size); 
    } else {
      std::future<uint64_t> future = std::async(&Ibverbs::get_remote_buffer, &ibv, 
                                                (uint64_t)local_buffer, local_buffer_key, 
                                                (uint64_t)remote_buffer, remote_buffer_key, 
                                                buffer_size); 
      futures.push_back(move(future));
    }
  }

  // WAIT for threads to complete
  std::vector< std::future<uint64_t> >::iterator future_iter;
  for( future_iter = futures.begin(); future_iter != futures.end(); ++future_iter ) {
    (*future_iter).wait();
  }

  // CALL the function to process the buffers
  processing_callback(dst_buffers);

#ifdef TIME_MEASUREMENT
  t2 = std::chrono::high_resolution_clock::now();
  elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);
#endif
  
  release_callback(dst_buffers);
  completed_transfers_lock.lock();
  std::list< std::tuple<void *, size_t, uint32_t> >::iterator iter;
  for( iter = buffers->begin(); iter != buffers->end(); ++iter ) {
    completed_transfers.push(std::get<0>(*iter));
  }
  completed_transfers_lock.unlock();

#ifdef TIME_MEASUREMENT
  size_t total_remote_size = std::accumulate(buffer_sizes.begin(), buffer_sizes.end(), 0);
  return std::make_tuple(total_remote_size, elapsed.count());
#else
  return std::make_tuple(0, 0);
#endif
}

int Server::start(std::list< std::tuple<size_t,double> > *time_measurements)
{
  std::vector< std::future< std::tuple<size_t,double> > > futures;
  void *remote_buffer = nullptr;
  size_t remote_buffer_size = 0;
  uint32_t remote_buffer_key = 0;
  char buffer[BUFFER_SIZE];
  char ack_buffer[BUFFER_SIZE];
  int n;
  char *p;

  while(1) {
    /* RECEIVE remote buffer addr --OR-- COMPLETE message */
    memset(buffer, 0, BUFFER_SIZE);
    n = read(client_sockfd, buffer, BUFFER_SIZE-1);
    dbg_connection("RECEIVED %d BYTES\n", n);
    if( strncmp(buffer, (char *)"COMPLETE", BUFFER_SIZE) == 0 ) {
      dbg_connection("RECEIVED client's COMPLETE request --> wait for FUTUREs\n");

      // MAKE sure all of the threads have completed
      std::vector<std::future<std::tuple<size_t,double>>>::iterator it;
      if( time_measurements == nullptr ) {
        for( it = futures.begin(); it != futures.end(); ++it ) {
          (*it).wait();
        }
      } else {
        for( it = futures.begin(); it != futures.end(); ++it ) {
          std::tuple<size_t,double> t = (*it).get();
          time_measurements->push_back(t);
        }
      }
      futures.clear();
      dbg_connection("FUTUREs complete (%ld transfers)\n", completed_transfers.size());

      /* SEND ACK complete */
      memset(ack_buffer, 0, BUFFER_SIZE);
      n = sprintf(ack_buffer, "COMPLETE:");
      p = ack_buffer;

      completed_transfers_lock.lock();
      while( !completed_transfers.empty() ) {
        p += n;
        void *ptr = completed_transfers.front();
        completed_transfers.pop();
        n = sprintf(p,"%p:", ptr);
      }
      completed_transfers_lock.unlock();

      dbg_connection("SEND ACK COMPLETE = %s\n", ack_buffer);
      n = write(client_sockfd, ack_buffer, strlen(ack_buffer));
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

      /* CHECK for ACTIVE memory buffers */
      if( active_registered_memory.size() != 0 ) {
        printf("[SERVER] ERROR: ACTIVE buffers found during shutdown\n");
        fflush(stdout);
        return -1;
      }
      return 0;
    } else if( n <= 0 ) {
      dbg_connection("CLIENT socket closed unexpectedly (%s)\n", strerror(errno));
      return -1;
    }

    dbg_unused(n);
    dbg_connection("RECEIVE %d bytes\n", n);
    dbg_connection("RECEIVE RDMA remote buffer = %s\n", buffer);

    char *string_token, *buffer_token;

    /* SEND ACK */
    memset(ack_buffer, 0, 256);
    char *p = ack_buffer;
    n = sprintf(ack_buffer, "ACK:");

    completed_transfers_lock.lock();
    while( !completed_transfers.empty() ) {
      p += n;
      void *ptr = completed_transfers.front();
      completed_transfers.pop();
      n = sprintf(p,"%p:", ptr);
    }
    completed_transfers_lock.unlock();

    dbg_connection("SEND ACK RDMA = %s\n", ack_buffer);
    n = write(client_sockfd, ack_buffer, strlen(ack_buffer));
    dbg_unused(n);
    dbg_connection("SEND %d bytes\n", n);

    std::list<char *> buffer_tokens;
    buffer_token = strtok(buffer,",");
    while( buffer_token != NULL ) {
      buffer_tokens.push_back(buffer_token);
      buffer_token = strtok(NULL,",");
    }

    std::list<char *>::iterator iter;
    std::list< std::tuple<void *, size_t, uint32_t> > *buffer_list =
      new std::list< std::tuple<void *, size_t, uint32_t> >;

    for( iter = buffer_tokens.begin(); iter != buffer_tokens.end(); ++iter ) {
      string_token = strtok((*iter),":");
      remote_buffer = (void *)strtoul(string_token, NULL, 16);
      string_token = strtok(NULL,":");
      remote_buffer_size = strtoul(string_token, NULL, 16);
      string_token = strtok(NULL,":");
      remote_buffer_key = strtoul(string_token, NULL, 16);
      buffer_list->push_back(std::make_tuple(remote_buffer,remote_buffer_size,remote_buffer_key));
    }
      
    std::future<std::tuple<size_t,double>> future = std::async(&Server::get_remote_buffers, this, buffer_list);
    futures.push_back(move(future));
    buffer_token = strtok(NULL,",");
  }
}

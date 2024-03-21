// Copyright 2022 National Technology & Engineering Solutions of Sandia, LLC 
// (NTESS).  Under the terms of Contract DE-NA0003525 with NTESS, the U.S. 
// Government retains certain rights in this software.

#include "Ibverbs.hpp"
#include <string>
#include <stdio.h>
#include <queue>
#include <list>
#include <map>
#include <utility> // std::pair, std::make_pair
#include <functional> // std::function
#include <mutex>

void empty_processing_callback(std::list< std::tuple<void *,size_t,uint32_t> >&) {};
void empty_cleanup_callback(void *) {};

class Server {
  public:
    Server();
    ~Server();
    int init(std::string addr_str, std::string *init_msg = nullptr);
    int ready();

    int add_memory(void *rdma_buffer, size_t rdma_buffer_size);
    int remove_memory(unsigned char *rdma_buffer);
    struct ibv_mr *register_memory(unsigned char *rdma_buffer, size_t rdma_buffer_size);
    void deregister_memory(struct ibv_mr *rdma_buffer);

    void set_buffer_processing_callback(std::function<void(std::list< std::tuple< void *,size_t,uint32_t> >&) > cb);
    void set_buffer_allocator_callback(std::function<void(std::list<size_t>, 
                                                          std::list< std::tuple<void *,size_t,uint32_t> >&) > cb);
    void set_buffer_release_callback(std::function<void(std::list< std::tuple<void *,size_t,uint32_t> >&)>);
    void set_buffer_cleanup_callback(std::function<void(struct ibv_mr *)> cb);
    void set_threading(bool threading) { threading_enabled = threading; }

    int start(std::list< std::tuple<size_t,double> > *time_measurements = nullptr);
  private:
    Ibverbs ibv;
    int client_sockfd;
    std::queue<void *> completed_transfers;
    std::mutex completed_transfers_lock;

    std::list< struct ibv_mr *> idle_registered_memory;
    std::list< struct ibv_mr *> active_registered_memory;
    std::map< void *, struct ibv_mr *> rdma_buffers;
    std::mutex registered_memory_lists_lock;

    std::function<void(std::list< std::tuple<void *,size_t,uint32_t> >&) > processing_callback = 
      empty_processing_callback;

    // The CLEANUP callback can't call the deregister_memory() member function b/c the DTOR iterates over the idle list
    // and deregister_memory function removes elements from the list.  Therefore, it calls Verbs directly.
    std::function<void(struct ibv_mr *)> cleanup_callback = 
      [this](struct ibv_mr *mr) ->void{ ibv.deregister_memory(mr); free(mr->addr); };

    // ALLOCATOR must provide: address (void *), size (size_t), and rkey value (uint32_t) for each buffer.
    std::function<void(std::list<size_t>, std::list< std::tuple<void *,size_t,uint32_t> >&) > allocator_callback =
      [this](std::list<size_t> sizes, std::list< std::tuple<void *,size_t,uint32_t > >& buffers)
        { acquire_registered_rdma_buffers(sizes, buffers); };

    std::function<void(std::list< std::tuple<void *,size_t,uint32_t> >&)> release_callback = 
      [this](std::list< std::tuple<void *,size_t,uint32_t> >& buffers) 
        { release_registered_rdma_buffers(buffers); };

    bool threading_enabled;

    std::tuple<size_t, double>  get_remote_buffer(void *remote_buffer_addr, 
                                                 size_t remote_buffer_size,
                                                 uint32_t remote_buffer_key);
    std::tuple<size_t, double>  get_remote_buffers(std::list< std::tuple<void *, size_t, uint32_t> > *buffers);

    void acquire_registered_rdma_buffer(size_t buffer_size, std::tuple<void *, size_t, uint32_t >& buffer);
    void acquire_registered_rdma_buffers(std::list<size_t> buffer_sizes, 
                                         std::list< std::tuple<void *, size_t, uint32_t > >& buffers);
    int release_registered_rdma_buffer(std::tuple<void *,size_t,uint32_t>& buffer);
    int release_registered_rdma_buffers(std::list< std::tuple<void *,size_t,uint32_t> >& buffers);
};



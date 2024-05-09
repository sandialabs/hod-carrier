// Copyright 2022 National Technology & Engineering Solutions of Sandia, LLC 
// (NTESS).  Under the terms of Contract DE-NA0003525 with NTESS, the U.S. 
// Government retains certain rights in this software.

#include "Ibverbs.hpp"
#include <string>
#include <utility>
#include <functional>
#include <tuple>
#include <queue>
#include <list>
#include <map>
#include <utility>

class Client {
  public:
    Client();
    int init(std::string init_msg = std::string());
    int ready();
    struct ibv_mr *register_memory(unsigned char *rdma_buffer, size_t rdma_buffer_size);
    void deregister_memory(struct ibv_mr *rdma_buffer);
    int request_transfer(struct ibv_mr *local_ibv_mr, size_t rdma_buffer_size,
                         std::function<void(struct ibv_mr *)> callback);
    int request_transfer(std::list<std::pair<struct ibv_mr *,size_t> > &buffer_list,
                         std::function<void(struct ibv_mr *)> callback);
    int stop();
  private:
    Ibverbs ibv;
    int host_sockfd;
    int server_sockfd;

    std::map< void *, std::tuple< struct ibv_mr *, 
                                  std::function<void(struct ibv_mr *)> > > pending_transfers;
    std::map<unsigned char *,struct ibv_mr *> registered_memory;

    void process_completed_buffer_list(char *buffer_list);
};



// Copyright 2022 National Technology & Engineering Solutions of Sandia, LLC 
// (NTESS).  Under the terms of Contract DE-NA0003525 with NTESS, the U.S. 
// Government retains certain rights in this software.

#include "Ibverbs.hpp"
#include <string>
#include <stdio.h>
#include <queue>
#include <mutex>

class Server {
  public:
    Server();
    int init(std::string addr_str);
    int start();
    uint64_t  get_remote_buffer(uint64_t remote_buffer_addr, size_t remote_buffer_size,
                                uint32_t remote_buffer_key);
  private:
    Ibverbs ibv;
    int client_sockfd;

    std::queue<uint64_t> completed_transfers;
    std::mutex completed_transfers_lock;
};



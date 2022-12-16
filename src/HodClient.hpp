// Copyright 2022 National Technology & Engineering Solutions of Sandia, LLC 
// (NTESS).  Under the terms of Contract DE-NA0003525 with NTESS, the U.S. 
// Government retains certain rights in this software.

#include "Ibverbs.hpp"
#include <string>
#include <utility>
#include <queue>

class Client {
  public:
    Client() : ibv(), pending_transfers() {};
    int init();
    int run(char *buffer, size_t buffer_size);
    int stop();
  private:
    Ibverbs ibv;
    int host_sockfd;
    int server_sockfd;

    std::queue<std::pair<char *,struct ibv_mr *>> pending_transfers;
};



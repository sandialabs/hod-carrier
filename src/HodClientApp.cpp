// Copyright 2022 National Technology & Engineering Solutions of Sandia, LLC 
// (NTESS).  Under the terms of Contract DE-NA0003525 with NTESS, the U.S. 
// Government retains certain rights in this software.

#include "HodClient.hpp"
#include <stdio.h>
#include <stdlib.h> // srand, rand
#include <random>   // mt19937

int get_rdma_buffer(Client *c, size_t buffer_size, struct ibv_mr **buffer)
{
  unsigned char *rdma_buffer = (unsigned char *)malloc(buffer_size*sizeof(unsigned char));

  // SEED the PRNG
  unsigned char seed = rand();
  std::mt19937 mt(seed);

  rdma_buffer[0] =  seed;
  for(unsigned int i = 1; i < buffer_size; i++ ) {
    rdma_buffer[i] = mt() % 0x100;
  }

  *buffer = c->register_memory(rdma_buffer, buffer_size);
  return 0;
}

int main(int argc, char **argv)
{
  int rdma_buffer_size = 256*1024*1024;

  Client *client = new Client();
  client->init();
  struct ibv_mr *local_ibv_mr;

  /* TRANSFER multiple memory buffers */
  std::list<std::pair< struct ibv_mr *,size_t > > buffer_list;

  for( int i = 0; i < 2; i++ ) {
    get_rdma_buffer(client, rdma_buffer_size, &local_ibv_mr);
    buffer_list.push_back(std::make_pair(local_ibv_mr,rdma_buffer_size));
  }

  client->request_transfer(buffer_list, 
                           [&client](struct ibv_mr *p)->void { client->deregister_memory(p); 
                                                               free(p->addr); } );
  client->stop();
  return 0;
}

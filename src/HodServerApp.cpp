// Copyright 2022 National Technology & Engineering Solutions of Sandia, LLC 
// (NTESS).  Under the terms of Contract DE-NA0003525 with NTESS, the U.S. 
// Government retains certain rights in this software.

#include "HodServer.hpp"
#include <stdio.h>
#include <stdlib.h>  // srand, rand
#include <random>    // mt19937

#define RANDOMSEED 0xADEAFCAB

void verify_random_data(std::list< std::tuple< void *,size_t,uint32_t > >& buffers)
{
  std::list< std::tuple< void *,size_t,uint32_t > >::iterator iter;
  for( iter = buffers.begin(); iter != buffers.end(); ++iter ) {
    uint8_t *rx_buffer = (uint8_t *)std::get<0>(*iter);
    size_t rx_buffer_size = std::get<1>(*iter);

    // SEED the PRNG
    unsigned char seed = rx_buffer[0];
    std::mt19937 mt(seed);

    int count = 0;
    for( unsigned int i = 1; i < rx_buffer_size; i++ ) {
      unsigned char r = (char) (mt() % 0x100);
      if( rx_buffer[i] != r ) {
        printf("WHOOPS : (seed = 0x%x) data mismatch [%d] (RX = 0x%x / RAND = 0x%x)\n", 
               rx_buffer[0], i, rx_buffer[i], r);
        fflush(stdout);
        if( count++ > 10 ) return;
      }
    }
  }

  printf("*** [verify_random_data] SUCCESS ***\n");
  fflush(stdout);
}

void simple_test_and_verify(std::string client_addr_str)
{
  Server *server = new Server;
  fflush(stdout);

  server->init(client_addr_str);
  server->set_buffer_processing_callback(verify_random_data);
  server->set_buffer_cleanup_callback( [] (void *p)->void { free(p); });

  int rdma_buffer_size = 256;
  unsigned char *rdma_buffer = (unsigned char *)malloc(rdma_buffer_size);
  server->add_memory(rdma_buffer, rdma_buffer_size);

  server->start();
  printf("Server COMPLETE!\n");
  fflush(stdout);
}

int main(int argc, char **argv)
{
  const char *env_var_name = "IBVERBS_CLIENT_IP_ADDR";
  char *client_addr = getenv(env_var_name);
  if( client_addr == nullptr )  {
    printf("ERROR: environment variable (%s) not defined\n", env_var_name);
    return -1;
  }
  std::string client_addr_str(client_addr);

  simple_test_and_verify(client_addr);
  return 0;
}

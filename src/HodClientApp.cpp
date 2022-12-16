// Copyright 2022 National Technology & Engineering Solutions of Sandia, LLC 
// (NTESS).  Under the terms of Contract DE-NA0003525 with NTESS, the U.S. 
// Government retains certain rights in this software.

#include "HodClient.hpp"
#include <stdio.h>

int main(int argc, char **argv)
{
  char *rdma_buffer = (char *)malloc(256);
  memset(rdma_buffer, 0, 256);

  for(int i = 0; i < 256; i++ ) {
    rdma_buffer[i] = i ^ 0xFF;
  }

  Client *client = new Client();
  client->init();
#if 0
  client->run(rdma_buffer, 256);
#endif
  client->run(rdma_buffer, 256);
  client->stop();
  return 0;
}

// Copyright 2022 National Technology & Engineering Solutions of Sandia, LLC 
// (NTESS).  Under the terms of Contract DE-NA0003525 with NTESS, the U.S. 
// Government retains certain rights in this software.

#include "HodServer.hpp"
#include <stdio.h>

int main(int argc, char **argv)
{
  const char *env_var_name = "IBVERBS_CLIENT_IP_ADDR";

  Server *server = new Server;
  printf("Server STARTing!\n");
  fflush(stdout);

  char *client_addr = getenv(env_var_name);
  if( client_addr == nullptr )  {
    printf("ERROR: environment variable (%s) not defined\n", env_var_name);
    return -1;
  }
  std::string client_addr_str(client_addr);

  server->init(client_addr_str);
  server->start();
  printf("Server COMPLETE!\n");
  return 0;
}

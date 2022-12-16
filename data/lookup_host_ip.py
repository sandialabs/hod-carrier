#!/usr/bin/env python3

# Copyright 2022 National Technology & Engineering Solutions of Sandia, LLC 
# (NTESS).  Under the terms of Contract DE-NA0003525 with NTESS, the U.S. 
# Government retains certain rights in this software.

import socket
import sys

def lookup_host_ip(hostname):
  ipaddr = socket.gethostbyname(hostname)
  return ipaddr
  

if __name__ == "__main__":
  ipaddr = lookup_host_ip(sys.argv[1])
  print(ipaddr)

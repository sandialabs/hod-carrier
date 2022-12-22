#!/usr/bin/env python3

# Copyright 2022 National Technology & Engineering Solutions of Sandia, LLC 
# (NTESS).  Under the terms of Contract DE-NA0003525 with NTESS, the U.S. 
# Government retains certain rights in this software.

import re
import sys

def get_bf_hostname(hostname):
  # <hostname> is the name of the Slurm-visible host associated with the BF
  # Assumption is that hostname is a text string followed by a number
  hostname_re = re.compile(".*[^\d](?P<number>\d+)")
  hostname_match = hostname_re.match(hostname)
  assert(hostname_match != None)
  bf_hostname = "bf" + hostname_match.group("number")
  return bf_hostname

if __name__ == "__main__":
  ipaddr = get_bf_hostname(sys.argv[1])
  print(ipaddr)

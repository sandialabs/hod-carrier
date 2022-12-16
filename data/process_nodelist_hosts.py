#!/usr/bin/env python3

# Copyright 2022 National Technology & Engineering Solutions of Sandia, LLC 
# (NTESS).  Under the terms of Contract DE-NA0003525 with NTESS, the U.S. 
# Government retains certain rights in this software.

import re
import sys
import os

# NOTE : this scripts assumes that the hostname of the BF2 nodes can be determined by appending the
# Slurm-visible hostname with "-bf"

nodelist_re = re.compile("(?P<partition>[a-z-]+(?:-bf)?)\[?(?P<nodenumbers>[0-9,-]+)\]?")
noderange_re = re.compile("(?P<start>\d+)-(?P<end>\d+)")

# STRING for aggregating results
result = ""

slurm_nodelist = os.environ['SLURM_JOB_NODELIST']
nodelist_strings = re.findall("[a-z-]+(?:-bf)?\[?[0-9,-]+\]?", slurm_nodelist)

for nodelist_string in nodelist_strings:
  if "-bf" in nodelist_string:
    continue
  nodelist_match = nodelist_re.match(nodelist_string)
  if nodelist_match != None:
    nodenumbers = nodelist_match.group("nodenumbers")
    partition = nodelist_match.group("partition")

    for nodenumber in nodenumbers.split(","):
      noderange_match = noderange_re.match(nodenumber)
      if noderange_match == None:
        start = int(nodenumber)
        end = int(nodenumber)
      else:
        start = int(noderange_match.group("start"))
        end = int(noderange_match.group("end"))

      for n in range(start,end+1):
        if len(result) > 0:
          result += " "
        result += ("%s%d" % (partition,n))
  else:
    print("WHOOPS: no match")
    exit(-1)

print(result)

#!/usr/bin/env python3

# Copyright 2022 National Technology & Engineering Solutions of Sandia, LLC 
# (NTESS).  Under the terms of Contract DE-NA0003525 with NTESS, the U.S. 
# Government retains certain rights in this software.

import re
import sys
import os

# [sllevy@klogin2 ~]$ srun -N 1 -n 1 echo $SLURM_JOB_NODELIST
# gn[3-4]

nodelist_re = re.compile("(?P<partition>[a-z-]+(?:-bf)?)\[?(?P<nodenumbers>[0-9,-]+)\]?")
noderange_re = re.compile("(?P<start>\d+)-(?P<end>\d+)")

# STRING for aggregating results
result = ""

"""
if len(sys.argv) == 2:
  slurm_nodelist = sys.argv[1]
  ppn = int(sys.argv[2])
else:
  print("USAGE: %s <nodelist> <ppn>" % (sys.argv[0]))
  exit(-1)
"""

slurm_nodelist = os.environ['SLURM_JOB_NODELIST']
nodelist_strings = re.findall("[a-z-]+(?:-bf)?\[?[0-9,-]+\]?", slurm_nodelist)

## There should be BlueField nodes
# !!!! DEEbug !!!!
#assert any(map(lambda x: "-bf" in x, nodelist_strings))

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
          #result += ","
          result += " "
        #result += ("%s%d:%d" % (partition,n,ppn))
        result += ("%s%d" % (partition,n))
  else:
    print("WHOOPS: no match")
    exit(-1)

print(result)

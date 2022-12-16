#!/usr/bin/env bash

# Copyright 2022 National Technology & Engineering Solutions of Sandia, LLC 
# (NTESS).  Under the terms of Contract DE-NA0003525 with NTESS, the U.S. 
# Government retains certain rights in this software.

# ALL command line arguments to this script will be passed to the salloc command
# For example, on kahuna at SNL calling this script with "-p glinda" will run it on the partition
# that includes BF2 devices.
salloc -N 1 $@ ./bluefield_test.sh 

#!/usr/bin/env bash

# Copyright 2022 National Technology & Engineering Solutions of Sandia, LLC 
# (NTESS).  Under the terms of Contract DE-NA0003525 with NTESS, the U.S. 
# Government retains certain rights in this software.

PWD=`pwd`
HOSTLIST=`./process_nodelist_hosts.py`
CLIENTEXE=${PWD}/../src/build-host/hod_client_app
SERVEREXE=${PWD}/../src/build-bf/hod_server_app

CLIENT_PORT=65432
BF_LIBRARY_PATH=${LD_LIBRARY_PATH}:${PWD}/../src/build-bf/
ENV_VARS="export LD_LIBRARY_PATH=$BF_LIBRARY_PATH; "
ENV_VARS+="export IBVERBS_CLIENT_SOCKET_PORT=${CLIENT_PORT}; "

echo "SLURM_JOB_NODELIST = $SLURM_JOB_NODELIST" 

for HOST in $HOSTLIST; do
  BF="${HOST}-bf"
  CLIENTADDR=`./lookup_host_ip.py $HOST`
  ENV_VARS+="export IBVERBS_CLIENT_IP_ADDR=$CLIENTADDR;"
  ENV_VARS+="export IBVERBS_SERVER_DEVICE=mlx5_0;"
  echo "CLIENTADDR = ${CLIENTADDR}"
  echo "ENV_VARS = ${ENV_VARS}"
  ssh -o "StrictHostKeyChecking no" $BF ${ENV_VARS} $SERVEREXE &
done

HOST_LIBRARY_PATH=${LD_LIBRARY_PATH}:${PWD}/../src/build-host/
echo "HOST_LIBRARY_PATH=${HOST_LIBRARY_PATH}"
export IBVERBS_CLIENT_SOCKET_PORT=${CLIENT_PORT}
export IBVERBS_CLIENT_SOCKET_IF=eth1
export LD_LIBRARY_PATH=$HOST_LIBRARY_PATH
srun -n 1 -N 1 $CLIENTEXE &

wait

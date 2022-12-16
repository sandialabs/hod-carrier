hod-carrier Overview
====================
**What is "hod-carrier"?**

The hod-carrier library is designed to move data between a host and a BlueField DPU using Infiniband
RDMA.  The basic design is that the DPU runs a server and the host runs a client.  The API is
structured so that a program executing on the host can issue a request to the server running on the
DPU to transfer data from host memory to BlueField2 DPU memory.

This repository contains the code necessary to build four components:
- *libhodclient* : a software library containing the code necessary to run an instance of a
  hod-carrier client

- *libhodserver* : a software library containing the code necessary to run an instance of a
  hod-carrier server

- *hod_client_app* : a simple example of an executable that launches a hod-carrier client and uses
  it request the server running on a BlueField DPU to transfer data from host memory to DPU memory.

- *hod_server_app* : a simple example of an executable that launches a hod-carrier server and
  services requests from a client instance running on the host processor.

**Why "hod-carrier"?**

A hod carrier is a person who delivers construction supplies (e.g., mortar, brick, stone) to
bricklayers and stone masons, commonly by employing a brick hod.  A brick hod is an exceedingly
simple device to simplify the transport of these materials.  Similarly, this software library is a
very simple device for moving data between a host and a BlueField DPU.  As use cases emerge for
this service, we expect that the features and sophistication of this library will grow.

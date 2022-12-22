// This file contains code based on taken from FAODEL (https://github.com/faodel/) that is subject
// to the following copyright:
//
// Copyright 2021 National Technology & Engineering Solutions of Sandia, LLC
// (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.

#include "Ibverbs.hpp"
#include <atomic>
#include <future>
#include <stdio.h>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <netdb.h>
#include <assert.h>
#include <errno.h>

#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define DEBUG_CONNECTION

#ifdef DEBUG_CONNECTION
#define dbg_connection(...) {printf("[IBVERBS] "); printf(__VA_ARGS__); fflush(stdout);}
#define dbg_connection_raw(...) {printf(__VA_ARGS__); fflush(stdout);}
#else
#define dbg_connection(...) 
#define dbg_connection_raw(...)
#endif

// In some cases, we want to use variables in debug messages that aren't otherwise used.
// This macro can be used to silence the compiler warnings associated with variables used this way
#ifdef DEBUG_CONNECTION
#define dbg_unused(v)
#else
#define dbg_unused(v) (void)v
#endif

// SETTERS & GETTERS
uint16_t Ibverbs::get_lid()
{
  return nic_lid_;
}

int Ibverbs::set_peer_lid(uint16_t peer_lid)
{
  peer_nic_lid_ = peer_lid;
  return 0;
}

uint32_t Ibverbs::get_cmd_qpn()
{
  return cmd_qp_->qp_num; 
}

int Ibverbs::set_peer_cmd_qpn(uint32_t cmd_qpn)
{
  peer_cmd_qpn_ = cmd_qpn;
  return 0;
}


uint32_t Ibverbs::get_rdma_qpn()
{
  return rdma_qp_->qp_num; 
}

int Ibverbs::set_peer_rdma_qpn(uint32_t rdma_qpn)
{
  peer_rdma_qpn_ = rdma_qpn;
  return 0;
}


// IBVERBS operations
int Ibverbs::setup_command_channel(void)
{
  int flags;

  printf("[IBVERBS] CALL ibv_create_comp_channel (cqe_count_ = %d)\n", cqe_count_);
  cmd_comp_channel_ = ibv_create_comp_channel(ctx_);
  if (!cmd_comp_channel_) {
    printf("[IBVERBS] ibv_create_comp_channel failed\n");
    return -1;
  }    
  cmd_cq_ = ibv_create_cq( ctx_, cqe_count_, NULL, cmd_comp_channel_, 0);  
  if (!cmd_cq_) {
    printf("[IBVERBS] ibv_create_cq failed: %s\n", strerror(errno));
    return -1;
  }    

  cmd_srq_count_=0;

  if (ibv_req_notify_cq(cmd_cq_, 0)) {
    printf("[IBVERBS]   ibv_req_notify_cq failed\n");
    return -1;
  }    

  /* use non-blocking IO on the async fd and completion fd */
  flags = fcntl(ctx_->async_fd, F_GETFL);
  if (flags < 0) { 
    printf("[IBVERBS]   failed to get async_fd flags\n");
    return -1;
  }    
  if (fcntl(ctx_->async_fd, F_SETFL, flags | O_NONBLOCK) < 0) { 
    printf("[IBVERBS]   failed to set async_fd to nonblocking\n");
    return -1;
  }    

  flags = fcntl(cmd_comp_channel_->fd, F_GETFL);
  if (flags < 0) { 
    printf("[IBVERBS]   failed to get completion fd flags\n");
    return -1;
  }    

  if (fcntl(cmd_comp_channel_->fd, F_SETFL, flags | O_NONBLOCK) < 0) { 
    printf("[IBVERBS]   failed to set completion fd to nonblocking\n");
    return -1;
  }    

  return 0;
}

int Ibverbs::setup_rdma_channel(void)
{
  int flags;

  printf("[IBVERBS] ENTER setup_rdma_channel\n");
  fflush(stdout);

  printf("[IBVERBS] CALL ibv_create_comp_channel\n");
  fflush(stdout);
  rdma_comp_channel_ = ibv_create_comp_channel(ctx_);
  if (!rdma_comp_channel_) {
    printf("[ibverbs_transport]   ibv_create_comp_channel failed\n");
    return -1;
  }    
 
  printf("[IBVERBS:%d] CALL ibv_create_cq (cqe_count_ = %d)\n", __LINE__, cqe_count_);
  fflush(stdout);
  rdma_cq_ = ibv_create_cq(ctx_, cqe_count_, NULL, rdma_comp_channel_, 0);  
  if (!rdma_cq_) {
    printf("[IBVERBS]   ibv_create_cq failed\n");
    return -1;
  }    

  rdma_srq_count_=0;

  printf("[IBVERBS] CALL ibv_req_notify_cq\n");
  fflush(stdout);
  if (ibv_req_notify_cq(rdma_cq_, 0)) {
    printf("[IBVERBS]   ibv_req_notify_cq failed\n");
    return -1;
  }    

  /* use non-blocking IO on the async fd and completion fd */
  printf("[IBVERBS] GET async_fd flags\n");
  fflush(stdout);
  flags = fcntl(ctx_->async_fd, F_GETFL);
  if (flags < 0) { 
    printf("[IBVERBS]   failed to get async_fd flags\n");
    return -1;
  }    

  printf("[IBVERBS] SET async_fd flags\n");
  fflush(stdout);
  if (fcntl(ctx_->async_fd, F_SETFL, flags | O_NONBLOCK) < 0) { 
    printf("[IBVERBS]   failed to set async_fd to nonblocking\n");
    return -1;
  }    

  printf("[IBVERBS] GET completion fd flags\n");
  fflush(stdout);
  flags = fcntl(rdma_comp_channel_->fd, F_GETFL);
  if (flags < 0) { 
    printf("[IBVERBS]   failed to get completion fd flags\n");
    return -1;
  }    

  printf("[IBVERBS] SET completion fd to nonblocking\n");
  fflush(stdout);
  if (fcntl(rdma_comp_channel_->fd, F_SETFL, flags | O_NONBLOCK) < 0) { 
    printf("[IBVERBS]   failed to set completion fd to nonblocking\n");
    return -1;
  }    

  printf("[IBVERBS] EXIT setup_rdma_channel\n");
  fflush(stdout);
  return 0;
}

int Ibverbs::transition_qp_from_reset_to_ready(
    struct ibv_qp *qp,
    uint32_t       peer_qpn,
    int            peer_lid,
    int            min_rnr_timer,
    int            ack_timeout,
    int            retry_count)
{   
  enum ibv_qp_attr_mask mask;
  struct ibv_qp_attr attr;
         
  dbg_connection("[ibverbs_connection] enter (qp=%p ; qp->qp_num=%u ; peer_qpn=%u ; peer_lid=%u)\n", 
                 qp, qp->qp_num, peer_qpn, peer_lid);
         
  /* Transition QP to Init */
  mask = (enum ibv_qp_attr_mask)
         ( IBV_QP_STATE
         | IBV_QP_ACCESS_FLAGS
         | IBV_QP_PKEY_INDEX
         | IBV_QP_PORT );
  memset(&attr, 0, sizeof(attr));
  attr.qp_state        = IBV_QPS_INIT;
  attr.pkey_index      = 0;
  attr.port_num        = nic_port_;
  attr.qp_access_flags =
         ( IBV_ACCESS_LOCAL_WRITE
         | IBV_ACCESS_REMOTE_WRITE
         | IBV_ACCESS_REMOTE_READ
         | IBV_ACCESS_REMOTE_ATOMIC ); 
  if (ibv_modify_qp(qp, &attr, mask)) {
    printf("[ibverbs_connection] failed to modify qp from RESET to INIT state\n");
    return -1;
  }      
         
  /* Transition QP to Ready-to-Receive (RTR) */
  mask = (enum ibv_qp_attr_mask)
         ( IBV_QP_STATE
         | IBV_QP_MAX_DEST_RD_ATOMIC
         | IBV_QP_AV
         | IBV_QP_PATH_MTU
         | IBV_QP_RQ_PSN
         | IBV_QP_DEST_QPN
         | IBV_QP_MIN_RNR_TIMER );
  memset(&attr, 0, sizeof(attr));
  attr.qp_state           = IBV_QPS_RTR;
  attr.max_dest_rd_atomic = 1;
  attr.ah_attr.dlid       = peer_lid;
  attr.ah_attr.port_num   = nic_port_;
  attr.path_mtu           = IBV_MTU_1024;
  attr.rq_psn             = 0;
  attr.dest_qp_num        = peer_qpn;
  attr.min_rnr_timer      = min_rnr_timer; /* delay before sending RNR NAK */
  if (ibv_modify_qp(qp, &attr, mask)) {
    printf("[ibverbs_connection] failed to modify qp from INIT to RTR state\n");
    return -1;
  }      
         
  /* Transition QP to Ready-to-Send (RTS) */
  mask = (enum ibv_qp_attr_mask)
         ( IBV_QP_STATE
         | IBV_QP_SQ_PSN
         | IBV_QP_MAX_QP_RD_ATOMIC
         | IBV_QP_TIMEOUT
         | IBV_QP_RETRY_CNT
         | IBV_QP_RNR_RETRY );
  memset(&attr, 0, sizeof(attr));
  attr.qp_state      = IBV_QPS_RTS; 
  attr.sq_psn        = 0;
  attr.max_rd_atomic = 1;
  attr.timeout       = ack_timeout;   /* time to wait for ACK/NAK before retransmitting.  
                                         4.096us * 2^ack_timeout */
  attr.retry_cnt     = retry_count; /* number of retries if no answer on primary path */
  attr.rnr_retry     = retry_count; /* number of retries if remote sends RNR NAK */

  if (ibv_modify_qp(qp, &attr, mask)) {
    printf("[ibverbs_connection] failed to modify qp from RTR to RTS state\n");
    return -1;
  }   

  dbg_connection("[ibverbs_connection] exit\n");
  return 0;
}

int Ibverbs::poll_cmd_cq()
{   
  int rc = 0;

  struct ibv_wc wc;

  struct timeval start, current;
  gettimeofday(&start, NULL);

  while( 1 ) {
    rc = ibv_poll_cq(cmd_cq_, 1, &wc);
    if( rc < 0 ) {
      printf("poll_cmd_cq ERROR\n");
      return -1;
    } else if( rc > 0 ) {
      // found a work completion
      if (wc.opcode == IBV_WC_SEND) {
        dbg_connection("SEND work completion = wc.wr_id (0x%lx) / status = %s\n", 
                       wc.wr_id, ibv_wc_status_str(wc.status));
        break;
      } else if (wc.opcode == IBV_WC_RECV) {
        dbg_connection("RECV work completion = wc.wr_id (0x%lx)\n", wc.wr_id);
        break;
      } else if (wc.opcode == IBV_WC_RDMA_READ) {
        dbg_connection("RDMA READ work completion = wc.wr_id (0x%lx) wc.status (%d / %s)\n", 
                       wc.wr_id, wc.status, ibv_wc_status_str(wc.status));
        break;
      } else {
        printf("poll_cmd_cq ERROR: UNRECOGNIZED opcode (STATUS = %d / OPCODE = 0x%x)\n", 
               wc.status, wc.opcode);
        return -1;
      }
    } else {
      gettimeofday(&current, NULL);
      if( current.tv_sec - start.tv_sec > 10 ) {
        printf("TIMED out!\n");
        return -1;
      }
    }
  }
  
  return (int)wc.opcode;
}

int Ibverbs::transition_to_ready(void)
{
  int min_rnr_timer;
  int ack_timeout;
  int retry_count;

  /* bring the two QPs up to RTR */

  min_rnr_timer = 1;  /* means 0.01ms delay before sending RNR NAK */
  ack_timeout   = 17; /* time to wait for ACK/NAK before retransmitting.  4.096us * 2^17 == 0.536s */
  retry_count   = 5;  /* number of retries if no answer on primary path or if remote sends RNR NAK.  
                         7 has special meaning of infinite retries. */

  dbg_connection("peer_nic_lid_ = %d\n", peer_nic_lid_);
  transition_qp_from_reset_to_ready(cmd_qp_,
                                    peer_cmd_qpn_,
                                    peer_nic_lid_,
                                    min_rnr_timer,
                                    ack_timeout,
                                    retry_count);

  min_rnr_timer = 31;  /* means 491.52ms delay before sending RNR NAK */
  ack_timeout   = 17;  /* time to wait for ACK/NAK before retransmitting.  4.096us * 2^17 == 0.536s */
  retry_count   = 5;   /* number of retries if no answer on primary path or if remote sends RNR NAK.  
                          7 has special meaning of infinite retries. */
  transition_qp_from_reset_to_ready(rdma_qp_,
                                    peer_rdma_qpn_,
                                    peer_nic_lid_,
                                    min_rnr_timer,
                                    ack_timeout,
                                    retry_count);
  return 0;
}

#if 0
Ibverbs::Ibverbs() : completed_transfers(), completed_transfers_lock() 
{ };
#endif

int Ibverbs::open_device(char *device_name)
{
  int ibv_rc = 0;
  struct ibv_device_attr dev_attr;
  struct ibv_port_attr   dev_port_attr;

  struct ibv_device **dev_list;
  int dev_count=0;

#if 0
  char *device_name = getenv("IBVERBS_SERVER_DEVICE");
  if( device_name != nullptr ) {
    dbg_connection("LOOKING for DEVICE --> %s\n", device_name);
  } else {
    printf("DEVICE name not specified\n");
    fflush(stdout);
    return -1;
  }
#endif

  dev_list = ibv_get_device_list(&dev_count);
  int i;
  for( i = 0; i < dev_count; i++ ) {
    if( device_name == nullptr || strcmp(device_name, dev_list[i]->name) == 0 ) {
      break;
    }
  }

  if (i >= dev_count) {
    if( device_name == nullptr ) {
      /* We should never get here, but say something just in case */
      printf("[IBVERBS] UNABLE to find any IB device\n");
    } else {
      printf("[IBVERBS] UNABLE to find device (%s)\n", device_name);
    }
    return -1;
  }

  dbg_connection("OPEN IB DEVICE --> %s\n", dev_list[i]->name);
  ctx_ = ibv_open_device(dev_list[i]);
  ibv_free_device_list(dev_list);
  nic_port_ = 1;

  /* get the lid and verify port state */
  ibv_rc = ibv_query_port(ctx_, nic_port_, &dev_port_attr);
  if (ibv_rc) {
    printf("[ibverbs_transport] ibv_query_port failed\n");
    return -1;
  }

  nic_lid_ = dev_port_attr.lid;

  if (dev_port_attr.state != IBV_PORT_ACTIVE) {
    printf("[ibverbs_transport] Could not find an active port.\n");
    return -1;
  }

  int active_mtu_bytes_=128;
  for (int i=0;i<dev_port_attr.active_mtu;i++) {
    active_mtu_bytes_ *= 2;
  }

  int cmd_msg_size_  = active_mtu_bytes_;
  int cmd_msg_count_ = 128;
  dbg_unused(cmd_msg_size_);
  dbg_unused(cmd_msg_count_);
  dbg_connection("dev_port_attr.active_mtu(%d) active_mtu_bytes_(%u) "
                 "cmd_msg_size_(%u) cmd_msg_count_(%u)\n",
                 dev_port_attr.active_mtu, active_mtu_bytes_, cmd_msg_size_, cmd_msg_count_);

  /* Query the device for device attributes (max QP, max WR, etc) */
  ibv_rc = ibv_query_device(ctx_, &dev_attr);
  if (ibv_rc) {
    printf("ibv_query_device failed\n");
    return -1;
  }

  cqe_count_ = dev_attr.max_cqe;
  dbg_connection("%d (max %d) completion queue entries\n", 
                 cqe_count_, dev_attr.max_cqe);

  //qp_count_ = dev_attr.max_qp_wr;
  qp_count_ = 1024;
  dbg_connection("max %d queue pair work requests\n", dev_attr.max_qp_wr);

  /* Allocate a Protection Domain (global) */
  dbg_connection("Allocate a PROTECTION DOMAIN\n");
  pd_ = ibv_alloc_pd(ctx_);
  if (!pd_) {
    printf("ibv_alloc_pd failed\n");
    return -1;
  }

  return 0;
}

int Ibverbs::create_qps()
{
  struct ibv_qp_init_attr attr;

  memset(&attr, 0, sizeof(attr));
  attr.qp_context       = NULL;
  attr.send_cq          = cmd_cq_; 
  attr.recv_cq          = cmd_cq_; 
  attr.cap.max_recv_wr  = qp_count_;
  attr.cap.max_send_wr  = qp_count_;
  attr.cap.max_recv_sge = 1;
  attr.cap.max_send_sge = 1;
  attr.qp_type          = IBV_QPT_RC;
  cmd_qp_ = ibv_create_qp(pd_, &attr);

  dbg_connection("Created CMD QP (%p)\n", cmd_qp_);

  memset(&attr, 0, sizeof(attr));
  attr.qp_context       = NULL;
  attr.send_cq          = rdma_cq_; 
  attr.recv_cq          = rdma_cq_; 
  attr.cap.max_recv_wr  = qp_count_;
  attr.cap.max_send_wr  = qp_count_;
  attr.cap.max_recv_sge = 1;
  attr.cap.max_send_sge = 1;
  attr.qp_type          = IBV_QPT_RC;
  rdma_qp_ = ibv_create_qp(pd_, &attr);

  dbg_connection("Created RDMA QP (%p)\n", rdma_qp_);

  if (ibv_req_notify_cq(rdma_cq_, 0)) {
    printf("[IBVERBS] Couldn't request CQ notification: %s\n", strerror(errno));
    return -1;
  }
  
  return 0;
}

#if 0
int Ibverbs::init(std::string addr_str)
{
  int rc     = 0;
  int ibv_rc = 0;
  struct ibv_device_attr dev_attr;
  struct ibv_port_attr   dev_port_attr;

  struct ibv_device **dev_list;
  int dev_count=0;

  char *device_name = getenv("IBVERBS_SERVER_DEVICE");
  if( device_name != nullptr ) {
    dbg_connection("LOOKING for DEVICE --> %s\n", device_name);
  } else {
    printf("DEVICE name not specified\n");
    fflush(stdout);
    return -1;
  }
  dev_list = ibv_get_device_list(&dev_count);
  int i;
  for( i = 0; i < dev_count; i++ ) {
    if( device_name == nullptr || strcmp(device_name, dev_list[i]->name) == 0 ) {
      break;
    }
  }

  if (i >= dev_count) {
    if( device_name == nullptr ) {
      /* We should never get here, but say something just in case */
      printf("[SERVER] UNABLE to find any IB device\n");
    } else {
      printf("[SERVER] UNABLE to find device (%s)\n", device_name);
    }
    return -1;
  }

  dbg_connection("OPEN IB DEVICE --> %s\n", dev_list[i]->name);
  ctx_ = ibv_open_device(dev_list[i]);
  ibv_free_device_list(dev_list);
  nic_port_ = 1;

  /* get the lid and verify port state */
  ibv_rc = ibv_query_port(ctx_, nic_port_, &dev_port_attr);
  if (ibv_rc) {
    printf("[ibverbs_transport] ibv_query_port failed\n");
    return -1;
  }

  nic_lid_ = dev_port_attr.lid;

  if (dev_port_attr.state != IBV_PORT_ACTIVE) {
    printf("[ibverbs_transport] Could not find an active port.\n");
    return -1;
  }

  int active_mtu_bytes_=128;
  for (int i=0;i<dev_port_attr.active_mtu;i++) {
    active_mtu_bytes_ *= 2;
  }


  int cmd_msg_size_  = active_mtu_bytes_;
  int cmd_msg_count_ = 128;
  dbg_unused(cmd_msg_size_);
  dbg_unused(cmd_msg_count_);
  dbg_connection("[ibverbs_transport] dev_port_attr.active_mtu(%d) active_mtu_bytes_(%u) "
                 "cmd_msg_size_(%u) cmd_msg_count_(%u)\n",
                 dev_port_attr.active_mtu, active_mtu_bytes_, cmd_msg_size_, cmd_msg_count_);

  /* Query the device for device attributes (max QP, max WR, etc) */
  ibv_rc = ibv_query_device(ctx_, &dev_attr);
  if (ibv_rc) {
    printf("[ibverbs_transport] ibv_query_device failed\n");
    return -1;
  }

  cqe_count_ = dev_attr.max_cqe;
  dbg_connection("[ibverbs_transport] %d (max %d) completion queue entries\n", 
                 cqe_count_, dev_attr.max_cqe);

  dbg_connection("[ibverbs_transport] max %d queue pair work requests\n", dev_attr.max_qp_wr);
  int qp_count_ = 1024;

  /* Allocate a Protection Domain (global) */
  dbg_connection("[ibverbs_transport] Allocate a PROTECTION DOMAIN\n");
  pd_ = ibv_alloc_pd(ctx_);
  if (!pd_) {
    printf("[ibverbs_transport] ibv_alloc_pd failed\n");
    return -1;
  }    

  dbg_connection("[ibverbs_transport] Set up COMMAND CHANNEL\n");
  rc = setup_command_channel();
  if (rc) {
    printf("[ibverbs_transport] setup_command_channel failed");
    return -1;
  }

  dbg_connection("[ibverbs_transport] Set up RDMA CHANNEL\n");
  rc = setup_rdma_channel();
  if (rc) {
    printf("[ibverbs_transport] setup_rdma_channel failed");
    return -1;
  }

  dbg_connection("[ibverbs_transport] InfiniBand (ibverbs) Initialized\n");

  struct ibv_qp_init_attr attr;

  memset(&attr, 0, sizeof(attr));
  attr.qp_context       = NULL;
  attr.send_cq          = cmd_cq_; 
  attr.recv_cq          = cmd_cq_; 
  attr.cap.max_recv_wr  = qp_count_;
  attr.cap.max_send_wr  = qp_count_;
  attr.cap.max_recv_sge = 1;
  attr.cap.max_send_sge = 1;
  attr.qp_type          = IBV_QPT_RC;
  cmd_qp_ = ibv_create_qp(pd_, &attr);

  dbg_connection("[ibverbs_transport] Created CMD QP (%p)\n", cmd_qp_);

  memset(&attr, 0, sizeof(attr));
  attr.qp_context       = NULL;
  attr.send_cq          = rdma_cq_; 
  attr.recv_cq          = rdma_cq_; 
  attr.cap.max_recv_wr  = qp_count_;
  attr.cap.max_send_wr  = qp_count_;
  attr.cap.max_recv_sge = 1;
  attr.cap.max_send_sge = 1;
  attr.qp_type          = IBV_QPT_RC;
  rdma_qp_ = ibv_create_qp(pd_, &attr);

  dbg_connection("[ibverbs_transport] Created RDMA QP (%p)\n", rdma_qp_);

  if (ibv_req_notify_cq(rdma_cq_, 0)) {
    printf("[ibverbs_connection] Couldn't request CQ notification: %s\n", strerror(errno));
    return -1;
  }   

  // ------ SOCKET code -------
  int n;
  char *portno_string = getenv("IBVERBS_CLIENT_SOCKET_PORT");
  if( portno_string == nullptr ) {
    printf("ERROR: Client port (IBVERBS_CLIENT_SOCKET_PORT) not set\n");
    return -1;
  }
  int portno = atoi(portno_string);
  struct sockaddr_in client_addr;
  char buffer[256];

  /* SERVER code */
  client_sockfd = socket(AF_INET, SOCK_STREAM, 0);
  client_addr.sin_addr.s_addr = inet_addr(addr_str.c_str());
  client_addr.sin_family = AF_INET;
  client_addr.sin_port = htons(portno);
  dbg_connection("CONNECTing to %s / %d\n", addr_str.c_str(), portno);
  dbg_connection("CONNECTing to %s / %d\n", inet_ntoa(client_addr.sin_addr), portno);
  int number_of_attempts = 5;
  for( int i = 0; i < number_of_attempts; i++ ) {
    rc = connect(client_sockfd,(struct sockaddr *)&client_addr,sizeof(client_addr));
    if( rc == 0 ) {
      // we did it
      break;
    }
    sleep(1);
  }

  if( rc < 0 ) {
    printf("UNABLE to CONNECT (%d attempts / rc = %d / %s : errno = %d)\n", 
           number_of_attempts, rc, strerror(errno), errno);
    return -1;
  }

  /* RECEIVE RDMA qpn */
  memset(buffer, 0, 256);
  n = read(client_sockfd, buffer, 255);
  dbg_unused(n);
  dbg_connection("RECEIVE %d bytes (%s)\n", n, strerror(errno));
  dbg_connection("RECEIVE RDMA qpn = %s\n", buffer);
  peer_rdma_qpn_ = atoi(buffer);

  /* SEND RDMA qpn */
  memset(buffer, 0, 256);
  sprintf(buffer, "%d", rdma_qp_->qp_num); 
  dbg_connection("SEND RDMA qpn = %s\n", buffer);
  n = write(client_sockfd, buffer, strlen(buffer));
  dbg_unused(n);
  dbg_connection("SEND %d bytes\n", n);

  /* RECEIVE CMD qpn */
  memset(buffer, 0, 256);
  n = read(client_sockfd, buffer, 255);
  dbg_unused(n);
  dbg_connection("RECEIVE %d bytes (%s)\n", n, strerror(errno));
  dbg_connection("RECEIVE RDMA qpn = %s\n", buffer);
  peer_cmd_qpn_ = atoi(buffer);

  /* SEND CMD qpn */
  memset(buffer, 0, 256);
  sprintf(buffer, "%d", cmd_qp_->qp_num); 
  dbg_connection("SEND RDMA qpn = %s\n", buffer);
  n = write(client_sockfd, buffer, strlen(buffer));
  dbg_connection("SEND %d bytes\n", n);

  /* RECEIVE lid */
  memset(buffer, 0, 256);
  n = read(client_sockfd, buffer, 255);
  dbg_unused(n);
  dbg_connection("RECEIVE %d bytes (%s)\n", n, strerror(errno));
  dbg_connection("RECEIVE RDMA lid = %s\n", buffer);
  peer_nic_lid_ = atoi(buffer);

  /* SEND lid */
  memset(buffer, 0, 256);
  sprintf(buffer, "%d", nic_lid_); 
  dbg_connection("SEND RDMA lid = %s\n", buffer);
  n = write(client_sockfd, buffer, strlen(buffer));
  dbg_unused(n);
  dbg_connection("SEND %d bytes\n", n);

  rc = transition_to_ready();
  if( rc < 0 ) return -1;

  dbg_connection("[ibverbs_transport] READY\n");

  return 0;
}
#endif

#if 0
std::atomic<int> filenumber(0);
#endif
#define MAX_RDMA_SIZE (512*1024*1024)

uint64_t Ibverbs::get_remote_buffer(uint64_t remote_buffer_addr, size_t remote_buffer_size,
                                    uint32_t remote_buffer_key)
{
  char *rdma_buffer;
  struct ibv_mr *local_ibv_mr;

  /* SEND MR */
  rdma_buffer = (char *)malloc(remote_buffer_size);
  memset(rdma_buffer, 0, remote_buffer_size);
  local_ibv_mr = ibv_reg_mr(pd_, rdma_buffer, remote_buffer_size, 
                            IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);

  if( local_ibv_mr == NULL ) {
    printf("*** FAILED creating MR\n");
    return 0;
  }

  int rc;
  struct ibv_sge sge;
  struct ibv_send_wr send_wr;
  struct ibv_send_wr *bad_wr;

  send_wr.wr_id      = (uint64_t)0xDEADBEEF;
  send_wr.next       = nullptr;
  send_wr.sg_list    = &sge;
  send_wr.num_sge    = 1;
  send_wr.opcode     = IBV_WR_RDMA_READ;

  send_wr.send_flags = IBV_SEND_SIGNALED;
  send_wr.imm_data   = 0xABCDEF;

  uint64_t bytes_transferred = 0;
  while( bytes_transferred < remote_buffer_size ) {
    uint64_t bytes_remaining = remote_buffer_size-bytes_transferred;
    uint64_t transfer_size = (bytes_remaining > MAX_RDMA_SIZE) ?  MAX_RDMA_SIZE : bytes_remaining;
    send_wr.wr.rdma.remote_addr = (uint64_t)remote_buffer_addr + bytes_transferred;
    send_wr.wr.rdma.rkey        = remote_buffer_key;

    sge.addr   = (uint64_t)local_ibv_mr->addr;
    sge.length = transfer_size;
    sge.lkey   = local_ibv_mr->lkey;

    dbg_connection("-----------------------------------------\n");
    dbg_connection("send_wr.wr_id               = 0x%lx\n", send_wr.wr_id);
    dbg_connection("send_wr.next                = %p\n", send_wr.next);
    dbg_connection("send_wr.num_sge             = %d\n", send_wr.num_sge);
    dbg_connection("send_wr.sg_list->addr       = 0x%lx\n", send_wr.sg_list->addr);
    dbg_connection("send_wr.sg_list->length     = %d\n", send_wr.sg_list->length);
    dbg_connection("send_wr.sg_list->lkey       = 0x%x\n", send_wr.sg_list->lkey);
    dbg_connection("send_wr.wr.rdma.remote_addr = 0x%lx\n", send_wr.wr.rdma.remote_addr);
    dbg_connection("send_wr.wr.rdma.rkey        = 0x%x\n", send_wr.wr.rdma.rkey);
    dbg_connection("-----------------------------------------\n");
    
    rc = ibv_post_send(cmd_qp_, &send_wr, &bad_wr);
    if( rc != 0 ) {
      printf("**** ibv_post_send FAILED (%d : %s)\n", errno, strerror(errno));
      printf("**** ibv_post_send FAILED (%d : %s)\n", rc, strerror(rc));
      return 0;
    } else {
      dbg_connection("ibv_post_send SUCCESS (REMOTE addr = 0x%lx / rkey = 0x%x)\n", 
                     send_wr.wr.rdma.remote_addr, send_wr.wr.rdma.rkey);
    }
    rc = poll_cmd_cq();

    if( rc <  0 ) {
      printf("POLLING failed\n");
      return 0;
    } else {
      enum ibv_wc_opcode opcode = (enum ibv_wc_opcode)rc;

      if( opcode == (int)IBV_WC_RDMA_READ ) {
        dbg_connection("BUFFER contents:\n");
        for( int i = 0; i < 10; i++ ) {
          dbg_connection_raw("0x%x ", rdma_buffer[i]);
          //assert(rdma_buffer[i] == (i ^ 0xFF));
        }
        dbg_connection_raw("\n");
      } else {
        printf("WRONG opcode!!!\n");
        return 0;
      }
    }
    bytes_transferred += transfer_size;
  }

  dbg_connection("TOTAL transfer %ld bytes (remote buffer size = %ld)\n", 
                 bytes_transferred, remote_buffer_size);

  free(rdma_buffer);
  ibv_dereg_mr(local_ibv_mr);
#if 0
  completed_transfers_lock.lock();
  completed_transfers.push(remote_buffer_addr);
  completed_transfers_lock.unlock();
#endif
  return remote_buffer_addr;
}

struct ibv_mr *Ibverbs::register_memory(char *rdma_buffer, size_t rdma_buffer_size)
{
  int access = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE;
  struct ibv_mr *mr = ibv_reg_mr(pd_, rdma_buffer, rdma_buffer_size, access);

  if( mr == NULL ) {
    printf("*** FAILED creating MR\n");
    return nullptr;
  }

  return mr;
}

int Ibverbs::post_recv_wr(struct ibv_recv_wr *recv_wr)
{
  struct ibv_recv_wr *bad_wr;
  int rc;

  rc = ibv_post_recv(cmd_qp_, recv_wr, &bad_wr);
  if( rc < 0 ) {
    printf("ibv_post_recv() FAILED\n");
    return -1;
  } else {
    dbg_connection("ibv_post_recv() SUCCESS\n");
  }
  return 0;
}

#if 0
#define BUFFER_SIZE 256
int Ibverbs::start()
{
  std::vector<std::future<int>> futures;
  uint64_t remote_buffer_addr = (uint64_t)nullptr;
  size_t remote_buffer_size = 0;
  uint32_t remote_buffer_key = 0;
  char buffer[BUFFER_SIZE];
  int n;

  while(1) {
    /* RECEIVE remote buffer addr --OR-- COMPLETE message */
    memset(buffer, 0, 256);
    n = read(client_sockfd, buffer, 255);
    dbg_connection("RECEIVED %d BYTES\n", n);
    if( strncmp(buffer, (char *)"COMPLETE", 256) == 0 ) {
      dbg_connection("RECEIVED ACK --> wait for FUTUREs\n");

      // MAKE sure all of the threads have completed
      std::vector<std::future<int>>::iterator it;
      for( it = futures.begin(); it != futures.end(); ++it ) {
        (*it).wait();
      }
      futures.clear();
      dbg_connection("FUTUREs complete\n");

      /* SEND ACK complete */
      memset(buffer, 0, 256);
      sprintf(buffer, "COMPLETE");
      dbg_connection("SEND ACK COMPLETE\n");
      n = write(client_sockfd, buffer, strlen(buffer));
      dbg_unused(n);
      dbg_connection("SEND %d bytes\n", n);

      dbg_connection("CLOSING CLIENT socket (COMPLETE)\n");
      if( close(client_sockfd) != 0 ) {
        printf("ERROR: Failed to CLOSE client socket");
        fflush(stdout);
        return -1;
      }
      dbg_connection("CLIENT socket closed SUCCESSFULLY\n");
      client_sockfd = -1;
      return 0;
    } else if( n <= 0 ) {
      dbg_connection("CLIENT socket closed unexpectedly (%s)\n", strerror(errno));
      fflush(stdout);
      return -1;
    }

    dbg_unused(n);
    dbg_connection("RECEIVE %d bytes\n", n);
    dbg_connection("RECEIVE RDMA remote buffer = %s\n", buffer);

    char *string_token;

    string_token = strtok(buffer,":");
    remote_buffer_addr = strtoul(string_token, NULL, 16);
    string_token = strtok(NULL,":");
    remote_buffer_size = strtoul(string_token, NULL, 16);
    string_token = strtok(NULL,":");
    remote_buffer_key = strtoul(string_token, NULL, 16);
    
    /* SEND ACK */
    memset(buffer, 0, 256);
    char *p = buffer;
    int n;
    n = sprintf(buffer, "ACK");

    completed_transfers_lock.lock();
    while( !completed_transfers.empty() ) {
      p += n;
      uint64_t addr = completed_transfers.front();
      completed_transfers.pop();
      n = sprintf(p,":0x%lx", addr);
    }
    completed_transfers_lock.unlock();

    dbg_connection("SEND ACK RDMA remote buffer addr = %s\n", buffer);
    n = write(client_sockfd, buffer, strlen(buffer));
    dbg_unused(n);
    dbg_connection("SEND %d bytes\n", n);

    std::future<int> future = std::async(&Ibverbs::transfer_data, this, 
                                         remote_buffer_addr,
                                         remote_buffer_size, 
                                         remote_buffer_key);
    futures.push_back(move(future));
  }
  return 0;
}
#endif

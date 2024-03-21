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

#include <chrono>  // high_resolution_clock

//#define DEBUG_CONNECTION

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


// IBVERBS operations
int Ibverbs::setup_command_channel(void)
{
  int flags;

  dbg_connection("CALL ibv_create_comp_channel (cqe_count_ = %d)\n", cqe_count_);
  // !!!! TODO ibv_destroy_comp_channel !!!!
  cmd_comp_channel_ = ibv_create_comp_channel(ctx_);
  if (!cmd_comp_channel_) {
    printf("[IBVERBS] ibv_create_comp_channel failed\n");
    return -1;
  }    
  // !!!! TODO ibv_destroy_cq !!!!
  cmd_cq_ = ibv_create_cq( ctx_, cqe_count_, NULL, cmd_comp_channel_, 0);  
  if (!cmd_cq_) {
    printf("[IBVERBS] ibv_create_cq failed: %s\n", strerror(errno));
    return -1;
  }    

  cmd_srq_count_=0;
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
  struct ibv_port_attr dev_port_attr;
  if( ibv_query_port(ctx_, nic_port_, &dev_port_attr) != 0 ) {
      printf("FAILED to query NIC port\n");
      fflush(stdout);
      return -1;
  }   
         
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
         ( IBV_ACCESS_REMOTE_WRITE
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
  attr.path_mtu           = dev_port_attr.active_mtu;
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
  attr.path_mig_state = IBV_MIG_REARM;
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
  struct ibv_wc wc;
  while( ibv_poll_cq(cmd_cq_, 1, &wc) == 0 ) {}
  return (int)wc.opcode;

  int rc = 0;

  while( 1 ) {
    rc = ibv_poll_cq(cmd_cq_, 1, &wc);
    if( rc == 0 ) {
      continue;
    } else if( rc > 0 ) {
      // found a work completion
      if (wc.opcode == IBV_WC_RDMA_READ) {
        dbg_connection("RDMA READ work completion = wc.wr_id (0x%lx) wc.status (%d / %s)\n", 
                       wc.wr_id, wc.status, ibv_wc_status_str(wc.status));
        break;
      } else {
        printf("poll_cmd_cq ERROR: UNRECOGNIZED opcode (STATUS = %d / OPCODE = 0x%x)\n", 
               wc.status, wc.opcode);
        return -1;
      }
    } else if( rc < 0 ) {
      printf("poll_cmd_cq ERROR\n");
      return -1;
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

  min_rnr_timer = 12;  /* means 0.01ms delay before sending RNR NAK */
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

  return 0;
}

int Ibverbs::open_device(char *device_name)
{
  int ibv_rc = 0;
  struct ibv_device_attr dev_attr;
  struct ibv_port_attr   dev_port_attr;

  struct ibv_device **dev_list;
  int dev_count=0;

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

  /* Query the device for device attributes (max QP, max WR, etc) */
  ibv_rc = ibv_query_device(ctx_, &dev_attr);
  if (ibv_rc) {
    printf("ibv_query_device failed\n");
    return -1;
  }

  //cqe_count_ = dev_attr.max_cqe;
  cqe_count_ = 1024;
  dbg_connection("%d (max %d) completion queue entries\n", cqe_count_, dev_attr.max_cqe);

  //qp_count_ = dev_attr.max_qp_wr;
  qp_count_ = 1024;
  dbg_connection("max %d queue pair work requests\n", dev_attr.max_qp_wr);

  /* Allocate a Protection Domain (global) */
  dbg_connection("Allocate a PROTECTION DOMAIN\n");
  // !!!!! TODO dealloc !!!!
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
  return 0;
}

#define MAX_RDMA_SIZE (512*1024*1024)

uint64_t Ibverbs::get_remote_buffer(uint64_t local_buffer_addr, uint32_t local_buffer_key,
                                    uint64_t remote_buffer_addr, uint32_t remote_buffer_key,
                                    size_t remote_buffer_size)
{
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

  uint64_t bytes_transferred = 0;
  while( bytes_transferred < remote_buffer_size ) {
    uint64_t bytes_remaining = remote_buffer_size-bytes_transferred;
    uint64_t transfer_size = (bytes_remaining > MAX_RDMA_SIZE) ?  MAX_RDMA_SIZE : bytes_remaining;
    send_wr.wr.rdma.remote_addr = (uint64_t)remote_buffer_addr + bytes_transferred;
    send_wr.wr.rdma.rkey        = remote_buffer_key;

    sge.addr   = (uint64_t)local_buffer_addr + bytes_transferred;
    sge.length = transfer_size;
    sge.lkey   = local_buffer_key;

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
    if( rc != 0 ) abort();
    struct ibv_wc wc;

    while( 1 ) {
      rc = ibv_poll_cq(cmd_cq_, 1, &wc);
      if( rc == 0 ) continue;
      if( rc > 0 ) break;
      if( rc < 0 ) {printf("ABORT %d\n", __LINE__); abort();}
    }
    bytes_transferred += transfer_size;
  }
  dbg_connection("TOTAL transfer %ld bytes (remote buffer size = %ld)\n", 
                 bytes_transferred, remote_buffer_size);

  return remote_buffer_addr;
}

struct ibv_mr *Ibverbs::register_memory(void *rdma_buffer, size_t rdma_buffer_size, int access)
{
  struct ibv_mr *mr = ibv_reg_mr(pd_, rdma_buffer, rdma_buffer_size, access);

  if( mr == NULL ) {
    printf("*** FAILED creating MR (rdma_buffer = %p / size = %ld / access = Ox%x\n",
           rdma_buffer, rdma_buffer_size, access);
    printf("[IBVERBS] ibv_reg_mr failed: %s\n", strerror(errno));
    return nullptr;
  }

  return mr;
}

void Ibverbs::deregister_memory(struct ibv_mr *registered)
{
  ibv_dereg_mr(registered);
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

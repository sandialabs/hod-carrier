#include <infiniband/verbs.h>
#include <string>
#include <stdio.h>
#include <queue>
#include <mutex>

class Ibverbs {
  public:
    Ibverbs() {};
    int open_device(char *device_name);
    int setup_command_channel();
    int create_qps();
    int transition_to_ready(void);
    int transition_qp_from_reset_to_ready(struct ibv_qp *qp, uint32_t peer_qpn, int peer_lid,
                                          int min_rnr_timer, int ack_timeout, int retry_count);
    int poll_cmd_cq();
    struct ibv_mr *register_memory(void *rdma_buffer, size_t rdma_buffer_size, int access);
    void deregister_memory(struct ibv_mr *registered_memory);
    int post_recv_wr(struct ibv_recv_wr *recv_wr);
    uint64_t get_remote_buffer(uint64_t local_buffer_addr, uint32_t local_buffer_key,
                               uint64_t remote_buffer_addr, uint32_t remote_buffer_key,
                               size_t remote_buffer_size);
    uint16_t get_lid();
    int set_peer_lid(uint16_t peer_lid);

    uint32_t get_cmd_qpn();
    int set_peer_cmd_qpn(uint32_t cmd_qpn);

  private:
    struct ibv_device              *dev_;
    uint16_t                        nic_lid_;
    int                             nic_port_;

    uint16_t                        peer_nic_lid_;
    uint32_t                        peer_cmd_qpn_;

    struct ibv_context             *ctx_;
    struct ibv_pd                  *pd_;

    struct ibv_comp_channel        *cmd_comp_channel_;
    struct ibv_cq                  *cmd_cq_;
    struct ibv_srq                 *cmd_srq_;
    struct ibv_qp                  *cmd_qp_;

    uint32_t                        active_mtu_bytes_;

    uint32_t                        cqe_count_;
    uint32_t                        srq_count_;
    uint32_t                        sge_count_;
    uint32_t                        qp_count_;

    uint32_t                        cmd_srq_count_;

    uint32_t                        cmd_msg_size_;
    uint32_t                        cmd_msg_count_;

    int                             client_sockfd;
};

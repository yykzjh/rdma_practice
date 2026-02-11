/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <iomanip>
#include <algorithm>
#include <netdb.h>
#include <malloc.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/time.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <infiniband/verbs.h>

#include "include/utils.hpp"
#include "include/pingpong.hpp"

enum {
	PINGPONG_RECV_WRID = 1,
	PINGPONG_SEND_WRID = 2,

	MAX_QP = 256,
};

struct pingpong_context {
	struct ibv_context *dev_ctx;
	struct ibv_comp_channel *event_channel;
	struct ibv_pd *pd;
	struct ibv_mr *mr;
	struct ibv_cq *cq;
	struct ibv_srq *srq;
	struct ibv_qp *qps[MAX_QP];
	char *buf;
	int size;
	int send_flags;
	int num_qp;
	int rx_depth;
	int pending[MAX_QP];
	struct ibv_port_attr port_info;
};

struct pingpong_dest {
	uint16_t lid;
	uint32_t qpn;
	uint32_t psn;
	union ibv_gid gid;
};

static struct option long_options[] = {
	{ .name = "port", .has_arg = required_argument, .flag = nullptr, .val = 'p' },
	{ .name = "ib-dev", .has_arg = required_argument, .flag = nullptr, .val = 'd' },
	{ .name = "ib-port", .has_arg = required_argument, .flag = nullptr, .val = 'i' },
	{ .name = "size", .has_arg = required_argument, .flag = nullptr, .val = 's' },
	{ .name = "mtu", .has_arg = required_argument, .flag = nullptr, .val = 'm' },
	{ .name = "num-qp", .has_arg = required_argument, .flag = nullptr, .val = 'q' },
	{ .name = "rx-depth", .has_arg = required_argument, .flag = nullptr, .val = 'r' },
	{ .name = "iters", .has_arg = required_argument, .flag = nullptr, .val = 'n' },
	{ .name = "sl", .has_arg = required_argument, .flag = nullptr, .val = 'l' },
	{ .name = "gid-idx", .has_arg = required_argument, .flag = nullptr, .val = 'g' },
	{ .name = "use_event", .has_arg = no_argument, .flag = nullptr, .val = 'e' },
	{ .name = "odp", .has_arg = no_argument, .flag = nullptr, .val = 'o' },
	{ .name = "chk", .has_arg = no_argument, .flag = nullptr, .val = 'c' },
	{}
};

static void usage(const std::string &argv0)
{
	std::cout << "Usage:" << std::endl;
	std::cout << "  " << argv0 << "            start a server and wait for connection" << std::endl;
	std::cout << "  " << argv0 << " <host>     connect to server at <host>" << std::endl;
	std::cout << std::endl;
	std::cout << "Options:" << std::endl;
	std::cout << "  -p, --port=<port>      listen on/connect to port <port> (default 18515)" << std::endl;
	std::cout << "  -d, --ib-dev=<dev>     use IB device <dev> (default first device found)" << std::endl;
	std::cout << "  -i, --ib-port=<port>   use port <port> of IB device (default 1)" << std::endl;
	std::cout << "  -s, --size=<size>      size of message to exchange (default 4096)" << std::endl;
	std::cout << "  -m, --mtu=<size>       path MTU (default 1024)" << std::endl;
	std::cout << "  -q, --num-qp=<num>     number of QPs to use (default 16)" << std::endl;
	std::cout << "  -r, --rx-depth=<dep>   number of receives to post at a time (default 500)" << std::endl;
	std::cout << "  -n, --iters=<iters>    number of exchanges per QP(default 1000)" << std::endl;
	std::cout << "  -l, --sl=<sl>          service level value" << std::endl;
	std::cout << "  -e, --events           sleep on CQ events (default poll)" << std::endl;
	std::cout << "  -g, --gid-idx=<gid index> local port gid index" << std::endl;
	std::cout << "  -o, --odp		    use on demand paging" << std::endl;
	std::cout << "  -c, --chk              validate received buffer" << std::endl;
}

static std::shared_ptr<struct pingpong_context> init_pp_ctx(struct ibv_device *ib_dev, size_t size, int num_qp,
															unsigned int rx_depth, size_t page_size, uint8_t ib_port,
															bool use_event, bool use_odp)
{
	int qp_idx = 0;
	int access_flags = IBV_ACCESS_LOCAL_WRITE;

	// Create pingpong context
	std::shared_ptr<struct pingpong_context> pp_ctx = std::make_shared<struct pingpong_context>();
	pp_ctx->size = size;
	pp_ctx->num_qp = num_qp;
	pp_ctx->rx_depth = rx_depth;
	pp_ctx->send_flags = IBV_SEND_SIGNALED;
	// Allocate memory for the buffer
	pp_ctx->buf = static_cast<char *>(memalign(page_size, size));
	if (!pp_ctx->buf) {
		std::cerr << "Failed to allocate work buffer" << std::endl;
		return nullptr;
	}
	// Initialize the buffer
	memset(pp_ctx->buf, 0, size);

	// Open the device
	pp_ctx->dev_ctx = ibv_open_device(ib_dev);
	if (!pp_ctx->dev_ctx) {
		std::cerr << "Failed to open deivce: " << ibv_get_device_name(ib_dev) << std::endl;
		goto clean_buffer;
	}

	// Check if the device supports ODP
	if (use_odp) {
		struct ibv_device_attr_ex dev_attr_ex{};
		const uint32_t rc_odp_caps_mask = IBV_ODP_SUPPORT_SEND | IBV_ODP_SUPPORT_SRQ_RECV;
		// Query extended device attributes
		if (ibv_query_device_ex(pp_ctx->dev_ctx, NULL, &dev_attr_ex)) {
			std::cerr << "Failed to query the extended device attributes" << std::endl;
			goto clean_device;
		}
		// Check if the device supports ODP
		if (!(dev_attr_ex.odp_caps.general_caps & IBV_ODP_SUPPORT) ||
			(dev_attr_ex.odp_caps.per_transport_caps.rc_odp_caps & rc_odp_caps_mask) != rc_odp_caps_mask) {
			std::cerr << "The device doesn't support ODP or doesn't support send or SRQ receive with ODP" << std::endl;
			goto clean_device;
		}
		access_flags |= IBV_ACCESS_ON_DEMAND;
	}

	// Create the event channel
	if (use_event) {
		pp_ctx->event_channel = ibv_create_comp_channel(pp_ctx->dev_ctx);
		if (!pp_ctx->event_channel) {
			std::cerr << "Failed to create event channel" << std::endl;
			goto clean_device;
		}
	} else {
		pp_ctx->event_channel = nullptr;
	}

	// Create the protection domain
	pp_ctx->pd = ibv_alloc_pd(pp_ctx->dev_ctx);
	if (!pp_ctx->pd) {
		std::cerr << "Failed to allocate PD" << std::endl;
		goto clean_event_channel;
	}

	// Register the memory region
	pp_ctx->mr = ibv_reg_mr(pp_ctx->pd, pp_ctx->buf, size, access_flags);
	if (!pp_ctx->mr) {
		std::cerr << "Failed to register MR" << std::endl;
		goto clean_pd;
	}

	// Create the completion queue
	pp_ctx->cq = ibv_create_cq(pp_ctx->dev_ctx, rx_depth + num_qp, nullptr, pp_ctx->event_channel, 0);
	if (!pp_ctx->cq) {
		std::cerr << "Failed to create CQ" << std::endl;
		goto clean_mr;
	}

	// Create the shared receive queue
	{
		// Initialize the SRQ init attributes
		struct ibv_srq_init_attr srq_init_attr{};
		srq_init_attr.attr.max_wr = rx_depth;
		srq_init_attr.attr.max_sge = 1;
		// Create the SRQ
		pp_ctx->srq = ibv_create_srq(pp_ctx->pd, &srq_init_attr);
		if (!pp_ctx->srq) {
			std::cerr << "Failed to create SRQ" << std::endl;
			goto clean_cq;
		}
	}

	// Create multiple QPs
	for (qp_idx = 0; qp_idx < num_qp; ++qp_idx) {
		// Initialize the QP init attributes
		struct ibv_qp_init_attr qp_init_attr{};
		qp_init_attr.send_cq = pp_ctx->cq;
		qp_init_attr.recv_cq = pp_ctx->cq;
		qp_init_attr.srq = pp_ctx->srq;
		qp_init_attr.cap.max_send_wr = 1;
		qp_init_attr.cap.max_send_sge = 1;
		qp_init_attr.qp_type = IBV_QPT_RC;
		// Create the QP
		pp_ctx->qps[qp_idx] = ibv_create_qp(pp_ctx->pd, &qp_init_attr);
		if (!pp_ctx->qps[qp_idx]) {
			std::cerr << "Failed to create QP " << qp_idx << std::endl;
			goto clean_qps;
		}
		// Query the QP attributes
		struct ibv_qp_attr qp_attr{};
		ibv_query_qp(pp_ctx->qps[qp_idx], &qp_attr, IBV_QP_CAP, &qp_init_attr);
		// Attempt to enable send inline
		if (qp_init_attr.cap.max_inline_data >= static_cast<uint32_t>(size)) {
			pp_ctx->send_flags |= IBV_SEND_INLINE;
		}
	}

	// Modify the QP to INIT state
	for (qp_idx = 0; qp_idx < num_qp; ++qp_idx) {
		// Initialize the QP attributes
		struct ibv_qp_attr qp_attr{};
		qp_attr.qp_state = IBV_QPS_INIT;
		qp_attr.pkey_index = 0;
		qp_attr.port_num = ib_port;
		qp_attr.qp_access_flags = 0;
		// Modify the QP to INIT state
		if (ibv_modify_qp(pp_ctx->qps[qp_idx], &qp_attr,
						  IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)) {
			std::cerr << "Failed to modify QP to INIT state" << std::endl;
			goto clean_qps_full;
		}
	}

	return pp_ctx;

clean_qps_full:
	qp_idx = num_qp;

clean_qps:
	for (--qp_idx; qp_idx >= 0; --qp_idx) {
		ibv_destroy_qp(pp_ctx->qps[qp_idx]);
	}
	if (pp_ctx->srq) {
		ibv_destroy_srq(pp_ctx->srq);
	}

clean_cq:
	if (pp_ctx->cq) {
		ibv_destroy_cq(pp_ctx->cq);
	}

clean_mr:
	if (pp_ctx->mr) {
		ibv_dereg_mr(pp_ctx->mr);
	}

clean_pd:
	if (pp_ctx->pd) {
		ibv_dealloc_pd(pp_ctx->pd);
	}

clean_event_channel:
	if (pp_ctx->event_channel) {
		ibv_destroy_comp_channel(pp_ctx->event_channel);
	}

clean_device:
	ibv_close_device(pp_ctx->dev_ctx);

clean_buffer:
	free(pp_ctx->buf);

	return nullptr;
}

static int close_pp_ctx(std::shared_ptr<struct pingpong_context> pp_ctx, int num_qp)
{
	int qp_idx;

	for (qp_idx = 0; qp_idx < num_qp; ++qp_idx) {
		if (ibv_destroy_qp(pp_ctx->qps[qp_idx])) {
			std::cerr << "Failed to destroy QP[" << qp_idx << "]" << std::endl;
			return 1;
		}
	}

	if (ibv_destroy_srq(pp_ctx->srq)) {
		std::cerr << "Failed to destroy SRQ" << std::endl;
		return 1;
	}

	if (ibv_destroy_cq(pp_ctx->cq)) {
		std::cerr << "Failed to destroy CQ" << std::endl;
		return 1;
	}

	if (ibv_dereg_mr(pp_ctx->mr)) {
		std::cerr << "Failed to deregister MR" << std::endl;
		return 1;
	}

	if (ibv_dealloc_pd(pp_ctx->pd)) {
		std::cerr << "Failed to deallocate PD" << std::endl;
		return 1;
	}

	if (pp_ctx->event_channel) {
		if (ibv_destroy_comp_channel(pp_ctx->event_channel)) {
			std::cerr << "Failed to destroy completion channel" << std::endl;
			return 1;
		}
	}

	if (ibv_close_device(pp_ctx->dev_ctx)) {
		std::cerr << "Failed to release context" << std::endl;
		return 1;
	}

	free(pp_ctx->buf);

	return 0;
}

static int post_recv_wr(std::shared_ptr<struct pingpong_context> pp_ctx, int n)
{
	// Initialize the SGE list
	struct ibv_sge sg_list{};
	sg_list.addr = reinterpret_cast<uint64_t>(pp_ctx->buf);
	sg_list.length = static_cast<uint32_t>(pp_ctx->size);
	sg_list.lkey = pp_ctx->mr->lkey;
	// Initialize the receive work request
	struct ibv_recv_wr recv_wr{};
	recv_wr.wr_id = PINGPONG_RECV_WRID;
	recv_wr.sg_list = &sg_list;
	recv_wr.num_sge = 1;
	// Post n receive work requests
	struct ibv_recv_wr *bad_recv_wr = nullptr;
	int i = 0;
	for (i = 0; i < n; ++i) {
		if (ibv_post_srq_recv(pp_ctx->srq, &recv_wr, &bad_recv_wr)) {
			break;
		}
	}
	return i;
}

static int post_send_wr(std::shared_ptr<struct pingpong_context> pp_ctx, int qp_idx)
{
	// Initialize the SGE list
	struct ibv_sge sg_list{};
	sg_list.addr = reinterpret_cast<uint64_t>(pp_ctx->buf);
	sg_list.length = static_cast<uint32_t>(pp_ctx->size);
	sg_list.lkey = pp_ctx->mr->lkey;
	// Initialize the send work request
	struct ibv_send_wr send_wr{};
	send_wr.wr_id = PINGPONG_SEND_WRID;
	send_wr.sg_list = &sg_list;
	send_wr.num_sge = 1;
	send_wr.opcode = IBV_WR_SEND;
	send_wr.send_flags = static_cast<unsigned int>(pp_ctx->send_flags);
	// Post the send work request
	struct ibv_send_wr *bad_send_wr = nullptr;
	return ibv_post_send(pp_ctx->qps[qp_idx], &send_wr, &bad_send_wr);
}

static int client_exch_dests(const std::string &server_name, int port, int num_qp,
							 const std::vector<std::shared_ptr<struct pingpong_dest> > &local_dests,
							 std::vector<std::shared_ptr<struct pingpong_dest> > &remote_dests)
{
	// Convert the port to a string
	std::string service;
	try {
		service = std::to_string(port);
	} catch (...) {
		std::cerr << "Failed to convert client port to string" << std::endl;
		return 1;
	}
	// Get the address information
	struct addrinfo *res = nullptr;
	struct addrinfo hints{};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	int ret = getaddrinfo(server_name.c_str(), service.c_str(), &hints, &res);
	if (ret < 0) {
		std::cerr << gai_strerror(ret) << " for " << server_name << ":" << port << std::endl;
		return 1;
	}

	// Try to connect to the server
	int sockfd = -1;
	for (struct addrinfo *t = res; t; t = t->ai_next) {
		sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
		if (sockfd >= 0) {
			if (!connect(sockfd, t->ai_addr, t->ai_addrlen)) {
				break;
			}
		}
		close(sockfd);
		sockfd = -1;
	}
	// Free the address information
	freeaddrinfo(res);
	// Check if the connection was successful
	if (sockfd < 0) {
		std::cerr << "Failed to connect to " << server_name << ":" << port << std::endl;
		return 1;
	}

	// Send the local destination information to the server
	std::string wgid, msg;
	for (int qp_idx = 0; qp_idx < num_qp; ++qp_idx) {
		// Convert the transferred data to string
		gid_to_wire_gid(&local_dests[qp_idx]->gid, wgid);
		std::ostringstream oss;
		oss << std::hex << std::nouppercase << std::setfill('0') << std::setw(4) << (local_dests[qp_idx]->lid & 0xffff)
			<< ':' << std::setw(6) << (local_dests[qp_idx]->qpn & 0xffffff) << ':' << std::setw(6)
			<< (local_dests[qp_idx]->psn & 0xffffff) << ':' << wgid;
		msg = oss.str();
		// Send the message to the server
		if (rdma_practice::write_all(sockfd, msg.c_str(), msg.size()) != static_cast<ssize_t>(msg.size())) {
			std::cerr << "Failed to send message for QP " << qp_idx << " to server" << std::endl;
			goto out;
		}
	}

	// Receive the remote destination information from the server
	for (int qp_idx = 0; qp_idx < num_qp; ++qp_idx) {
		// Read the message from the server
		if (rdma_practice::read_all(sockfd, msg.data(), msg.size()) != static_cast<ssize_t>(msg.size())) {
			std::cerr << "Failed to read message for QP " << qp_idx << " from server" << std::endl;
			goto out;
		}
		// Initialize the remote destination for the QP
		std::shared_ptr<struct pingpong_dest> remote_dest = std::make_shared<struct pingpong_dest>();
		memset(remote_dest.get(), 0, sizeof(struct pingpong_dest));
		// Parse the message
		std::replace(msg.begin(), msg.end(), ':', ' ');
		std::istringstream iss(msg);
		std::string remote_lid, remote_qpn, remote_psn, remote_wgid;
		iss >> remote_lid >> remote_qpn >> remote_psn >> remote_wgid;
		remote_dest->lid = static_cast<uint16_t>(std::stoul(remote_lid, nullptr, 16));
		remote_dest->qpn = static_cast<uint32_t>(std::stoul(remote_qpn, nullptr, 16));
		remote_dest->psn = static_cast<uint32_t>(std::stoul(remote_psn, nullptr, 16));
		wire_gid_to_gid(remote_wgid, &remote_dest->gid);
		remote_dests.push_back(remote_dest);
	}

	if (rdma_practice::write_all(sockfd, "done", sizeof("done")) != sizeof("done")) {
		std::cerr << "Failed to send done message to server" << std::endl;
		goto out;
	}

	return 0;

out:
	close(sockfd);

	return 1;
}

static int modify_qp_state_rtr_rts(std::shared_ptr<struct pingpong_context> pp_ctx, int ib_port, enum ibv_mtu mtu,
								   int sl, int gid_idx, int num_qp,
								   const std::vector<std::shared_ptr<struct pingpong_dest> > &local_dests,
								   const std::vector<std::shared_ptr<struct pingpong_dest> > &remote_dests)
{
	for (int qp_idx = 0; qp_idx < num_qp; ++qp_idx) {
		// Prepare RTR QP state attributes
		struct ibv_qp_attr qp_attr{};
		qp_attr.qp_state = IBV_QPS_RTR;
		qp_attr.path_mtu = mtu;
		qp_attr.dest_qp_num = remote_dests[qp_idx]->qpn;
		qp_attr.rq_psn = remote_dests[qp_idx]->psn;
		qp_attr.max_dest_rd_atomic = 1;
		qp_attr.min_rnr_timer = 12;
		qp_attr.ah_attr.is_global = 0;
		qp_attr.ah_attr.dlid = remote_dests[qp_idx]->lid;
		qp_attr.ah_attr.sl = sl;
		qp_attr.ah_attr.src_path_bits = 0;
		qp_attr.ah_attr.port_num = ib_port;
		if (remote_dests[qp_idx]->gid.global.interface_id) {
			qp_attr.ah_attr.is_global = 1;
			qp_attr.ah_attr.grh.hop_limit = 1;
			qp_attr.ah_attr.grh.dgid = remote_dests[qp_idx]->gid;
			qp_attr.ah_attr.grh.sgid_index = gid_idx;
		}
		// Modify the QP to RTR state
		if (ibv_modify_qp(pp_ctx->qps[qp_idx], &qp_attr,
						  IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
							  IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER)) {
			std::cerr << "Failed to modify QP " << qp_idx << " to RTR state" << std::endl;
			return 1;
		}
		// Prepare RTS QP state attributes
		qp_attr.qp_state = IBV_QPS_RTS;
		qp_attr.timeout = 14;
		qp_attr.retry_cnt = 7;
		qp_attr.rnr_retry = 7;
		qp_attr.sq_psn = local_dests[qp_idx]->psn;
		qp_attr.max_rd_atomic = 1;
		// Modify the QP to RTS state
		if (ibv_modify_qp(pp_ctx->qps[qp_idx], &qp_attr,
						  IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
							  IBV_QP_MAX_QP_RD_ATOMIC)) {
			std::cerr << "Failed to modify QP " << qp_idx << " to RTS state" << std::endl;
			return 1;
		}
	}
	return 0;
}

static int server_exch_dests(std::shared_ptr<struct pingpong_context> pp_ctx, int port, int ib_port, enum ibv_mtu mtu,
							 int sl, int gid_idx, int num_qp,
							 const std::vector<std::shared_ptr<struct pingpong_dest> > &local_dests,
							 std::vector<std::shared_ptr<struct pingpong_dest> > &remote_dests)
{
	int connfd = -1;
	std::string wgid, send_msg;

	// Convert the port to a string
	std::string service;
	try {
		service = std::to_string(port);
	} catch (...) {
		std::cerr << "Failed to convert server port to string" << std::endl;
		return 1;
	}

	// Get address information
	struct addrinfo *res = nullptr;
	struct addrinfo hints {
	};
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	int ret = getaddrinfo(nullptr, service.c_str(), &hints, &res);
	if (ret < 0) {
		std::cerr << gai_strerror(ret) << " for port " << port << std::endl;
		return 1;
	}
	// Try to bind to the port
	int sockfd = -1;
	for (struct addrinfo *t = res; t; t = t->ai_next) {
		sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
		if (sockfd >= 0) {
			ret = 1;
			setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &ret, sizeof(ret));
			if (!bind(sockfd, t->ai_addr, t->ai_addrlen)) {
				break;
			}
			close(sockfd);
			sockfd = -1;
		}
	}
	// Free the address information
	freeaddrinfo(res);
	// Check if the binding was successful
	if (sockfd < 0) {
		std::cerr << "Failed to bind to port " << port << std::endl;
		return 1;
	}

	// Listen for connections
	ret = listen(sockfd, 1);
	if (ret < 0) {
		std::cerr << "Failed to listen for connection" << std::endl;
		close(sockfd);
		return 1;
	}

	// Accept one connection
	connfd = accept(sockfd, nullptr, nullptr);
	// Close the listening socket
	close(sockfd);
	if (connfd < 0) {
		std::cerr << "accept() failed" << std::endl;
		return 1;
	}

	// Receive the remote destination information from the client
	std::string msg(sizeof("0000:000000:000000:00000000000000000000000000000000"), '\0');
	for (int qp_idx = 0; qp_idx < num_qp; ++qp_idx) {
		// Read the message from the client
		ret = rdma_practice::read_all(connfd, msg.data(), msg.size());
		if (static_cast<size_t>(ret) != msg.size()) {
			std::cerr << ret << "/" << msg.size() << ": QP " << qp_idx << " failed to read message from client"
					  << std::endl;
			goto out;
		}
		// Initialize the remote destination for the QP
		std::shared_ptr<struct pingpong_dest> remote_dest = std::make_shared<struct pingpong_dest>();
		memset(remote_dest.get(), 0, sizeof(struct pingpong_dest));
		// Parse the message
		std::replace(msg.begin(), msg.end(), ':', ' ');
		std::istringstream iss(msg);
		std::string remote_lid, remote_qpn, remote_psn, remote_wgid;
		iss >> remote_lid >> remote_qpn >> remote_psn >> remote_wgid;
		remote_dest->lid = static_cast<uint16_t>(std::stoul(remote_lid, nullptr, 16));
		remote_dest->qpn = static_cast<uint32_t>(std::stoul(remote_qpn, nullptr, 16));
		remote_dest->psn = static_cast<uint32_t>(std::stoul(remote_psn, nullptr, 16));
		wire_gid_to_gid(remote_wgid, &remote_dest->gid);
		remote_dests.push_back(remote_dest);
	}

	// Connect to the client QPs
	if (modify_qp_state_rtr_rts(pp_ctx, ib_port, mtu, sl, gid_idx, num_qp, local_dests, remote_dests)) {
		std::cerr << "Failed to modify server QPs state to RTR/RTS" << std::endl;
		goto out;
	}

	// Send the local destination information to the client
	for (int qp_idx = 0; qp_idx < num_qp; ++qp_idx) {
		// Convert the transferred data to string
		gid_to_wire_gid(&local_dests[qp_idx]->gid, wgid);
		std::ostringstream oss;
		oss << std::hex << std::nouppercase << std::setfill('0') << std::setw(4) << (local_dests[qp_idx]->lid & 0xffff)
			<< ':' << std::setw(6) << (local_dests[qp_idx]->qpn & 0xffffff) << ':' << std::setw(6)
			<< (local_dests[qp_idx]->psn & 0xffffff) << ':' << wgid;
		send_msg = oss.str();
		// Send the message to the client
		if (rdma_practice::write_all(connfd, send_msg.c_str(), send_msg.size()) !=
			static_cast<ssize_t>(send_msg.size())) {
			std::cerr << "Failed to send message for QP " << qp_idx << " to client" << std::endl;
			goto out;
		}
	}

	// Receive the done message from the client
	if (rdma_practice::read_all(connfd, msg.data(), sizeof("done")) != sizeof("done")) {
		std::cerr << "Failed to read done message from client" << std::endl;
		goto out;
	}

	return 0;

out:
	remote_dests.clear();
	close(connfd);

	return 1;
}

static int find_qp(uint32_t qpn, std::shared_ptr<struct pingpong_context> pp_ctx, int num_qp)
{
	int i;

	for (i = 0; i < num_qp; ++i)
		if (pp_ctx->qps[i]->qp_num == qpn)
			return i;

	return -1;
}

static int process_wc(std::vector<struct ibv_wc> &wcs, int num_poll_wc, std::shared_ptr<struct pingpong_context> pp_ctx,
					  int num_qp, int *scnt, int *rcnt, int *routs, int iters)
{
	for (int i = 0; i < num_poll_wc; ++i) {
		// Check the work completion status
		if (wcs[i].status != IBV_WC_SUCCESS) {
			std::cerr << "Failed status " << ibv_wc_status_str(wcs[i].status) << " (" << wcs[i].status << ") for wr_id"
					  << wcs[i].wr_id << std::endl;
			return 1;
		}
		// Find the QP index
		int qp_idx = find_qp(wcs[i].qp_num, pp_ctx, num_qp);
		if (qp_idx < 0) {
			std::cerr << "QP " << wcs[i].qp_num << " not found" << std::endl;
			return 1;
		}
		// Parse the work completion according to the work request ID
		switch ((int)wcs[i].wr_id) {
		case PINGPONG_SEND_WRID: {
			++(*scnt);
			break;
		}
		case PINGPONG_RECV_WRID: {
			// If the number of receive work requests is less than or equal to num_qp, post more receive work requests
			if (--(*routs) <= num_qp) {
				(*routs) += post_recv_wr(pp_ctx, pp_ctx->rx_depth - (*routs));
				if ((*routs) < pp_ctx->rx_depth) {
					std::cerr << "Failed to post expected number of receive work requests: " << (*routs) << "/"
							  << pp_ctx->rx_depth << std::endl;
					return 1;
				}
			}
			++(*rcnt);
			break;
		}
		default: {
			std::cerr << "Work completion for unknown wr_id: " << wcs[i].wr_id << std::endl;
			return 1;
		}
		}
		// Update the pending state
		pp_ctx->pending[qp_idx] &= ~(int)wcs[i].wr_id;
		// Reset the pending work requests if the number of send and receive work requests is less than the number of iterations
		if (*scnt < iters && !pp_ctx->pending[qp_idx]) {
			if (post_send_wr(pp_ctx, qp_idx)) {
				std::cerr << "QP " << qp_idx << " failed to post send work request" << std::endl;
				return 1;
			}
			pp_ctx->pending[qp_idx] = PINGPONG_SEND_WRID | PINGPONG_RECV_WRID;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	unsigned int port = 18515;
	std::string ib_dev_name = "";
	uint8_t ib_port = 1;
	size_t size = 4096;
	enum ibv_mtu mtu = IBV_MTU_1024;
	int num_qp = 16;
	unsigned int rx_depth = 500;
	int iters = 1000;
	int sl = 0;
	int gid_idx = 1;
	bool use_event = false;
	bool use_odp = false;
	bool use_chk = false;
	size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
	std::shared_ptr<struct pingpong_context> pp_ctx = nullptr;
	int routs = 0;
	std::mt19937 rng = rdma_practice::make_rng();
	std::vector<std::shared_ptr<struct pingpong_dest> > local_dests;
	std::vector<std::shared_ptr<struct pingpong_dest> > remote_dests;
	struct timeval start, end;
	int scnt, rcnt;
	int num_cq_events = 0;

	// Parse the command line arguments
	while (true) {
		// Parse one command line argument
		int opt_char = getopt_long(argc, argv, "p:d:i:s:m:q:r:n:l:g:eoc", long_options, nullptr);
		// No more options to parse
		if (opt_char == -1) {
			break;
		}
		// Handle the option
		switch (opt_char) {
		case 'p': {
			try {
				port = std::stoul(optarg);
				if (port > 65535) {
					usage(argv[0]);
					return 1;
				}
			} catch (...) {
				std::cerr << "Invalid port number: " << optarg << std::endl;
				return 1;
			}
			break;
		}
		case 'd': {
			ib_dev_name = optarg;
			break;
		}
		case 'i': {
			try {
				ib_port = static_cast<uint8_t>(std::stoul(optarg));
				if (ib_port < 1) {
					usage(argv[0]);
					return 1;
				}
			} catch (...) {
				std::cerr << "Invalid IB port number: " << optarg << std::endl;
				return 1;
			}
			break;
		}
		case 's': {
			try {
				size = static_cast<size_t>(std::stoul(optarg));
				if (size < 1) {
					usage(argv[0]);
					return 1;
				}
			} catch (...) {
				std::cerr << "Invalid size: " << optarg << std::endl;
				return 1;
			}
			break;
		}
		case 'm': {
			try {
				mtu = pp_mtu_to_enum(std::stoi(optarg));
				if (mtu == static_cast<enum ibv_mtu>(0)) {
					usage(argv[0]);
					return 1;
				}
			} catch (...) {
				std::cerr << "Invalid MTU: " << optarg << std::endl;
				return 1;
			}
			break;
		}
		case 'q': {
			try {
				num_qp = std::stoi(optarg);
				if (num_qp < 1) {
					usage(argv[0]);
					return 1;
				}
			} catch (...) {
				std::cerr << "Invalid number of QPs: " << optarg << std::endl;
				return 1;
			}
			break;
		}
		case 'r': {
			try {
				rx_depth = std::stoul(optarg);
				if (rx_depth < 1) {
					usage(argv[0]);
					return 1;
				}
			} catch (...) {
				std::cerr << "Invalid RX depth: " << optarg << std::endl;
				return 1;
			}
			break;
		}
		case 'n': {
			try {
				iters = std::stoi(optarg);
				if (iters < 1) {
					usage(argv[0]);
					return 1;
				}
			} catch (...) {
				std::cerr << "Invalid number of iterations: " << optarg << std::endl;
				return 1;
			}
			break;
		}
		case 'l': {
			try {
				sl = std::stoi(optarg);
				if (sl < 0 || sl > 15) {
					usage(argv[0]);
					return 1;
				}
			} catch (...) {
				std::cerr << "Invalid SL: " << optarg << std::endl;
				return 1;
			}
			break;
		}
		case 'g': {
			try {
				gid_idx = std::stoi(optarg);
			} catch (...) {
				std::cerr << "Invalid GID index: " << optarg << std::endl;
				return 1;
			}
			break;
		}
		case 'e': {
			use_event = true;
			break;
		}
		case 'o': {
			use_odp = true;
			break;
		}
		case 'c': {
			use_chk = true;
			break;
		}
		default: {
			usage(argv[0]);
			return 1;
		}
		}
	}

	// Get the server name if launched as a client
	std::string server_name = "";
	if (optind == argc - 1) {
		server_name = argv[optind];
	} else if (optind < argc) {
		usage(argv[0]);
		return 1;
	}

	// Check if the RX depth is large enough for the number of QPs
	if (num_qp > static_cast<int>(rx_depth)) {
		std::cerr << "rx_depth " << rx_depth << " is too small for " << num_qp
				  << " QPs -- must have at least one receive per QP." << std::endl;
		return 1;
	}
	// Check if the number of QPs is valid
	if (num_qp > static_cast<int>(MAX_QP)) {
		std::cerr << "num_qp " << num_qp << " must be less than or equal to " << MAX_QP << std::endl;
		return 1;
	}

	// Prepare the work completion vector
	int num_wc = num_qp + static_cast<int>(rx_depth);
	std::vector<struct ibv_wc> wcs(num_wc);

	// Get the device
	int num_devices = 0;
	struct ibv_device *ib_dev = nullptr;
	struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list || num_devices == 0) {
		perror("Failed to get IB devices list");
		goto clean_dev_list;
	}
	if (ib_dev_name.empty()) {
		std::cerr << "No IB device name provided" << std::endl;
		goto clean_dev_list;
	}
	ib_dev = *dev_list;
	for (int i = 0; i < num_devices && ib_dev; ++i) {
		if (ib_dev_name == ibv_get_device_name(ib_dev))
			break;
		ib_dev = dev_list[i + 1];
	}
	if (!ib_dev) {
		std::cerr << "IB device " << ib_dev_name << " not found" << std::endl;
		goto clean_dev_list;
	}

	// Initialize the pingpong context
	pp_ctx = init_pp_ctx(ib_dev, size, num_qp, rx_depth, page_size, ib_port, use_event, use_odp);
	if (!pp_ctx) {
		std::cerr << "Failed to initialize pingpong context" << std::endl;
		goto clean_dev_list;
	}

	// Post enough receive work requests
	routs = post_recv_wr(pp_ctx, pp_ctx->rx_depth);
	if (routs < pp_ctx->rx_depth) {
		std::cerr << "Failed to post " << pp_ctx->rx_depth << " receive work requests" << std::endl;
		goto clean_pp_ctx;
	}
	for (int qp_idx = 0; qp_idx < num_qp; ++qp_idx) {
		pp_ctx->pending[qp_idx] = PINGPONG_RECV_WRID;
	}

	// Request CQ notification
	if (use_event) {
		if (ibv_req_notify_cq(pp_ctx->cq, 0)) {
			std::cerr << "Failed to request CQ notification" << std::endl;
			goto clean_pp_ctx;
		}
	}

	// Initialize the local destination information
	for (int qp_idx = 0; qp_idx < num_qp; ++qp_idx) {
		std::shared_ptr<struct pingpong_dest> local_dest = std::make_shared<struct pingpong_dest>();
		memset(local_dest.get(), 0, sizeof(struct pingpong_dest));
		local_dests.push_back(local_dest);
	}
	// Get the port attributes
	if (pp_get_port_info(pp_ctx->dev_ctx, ib_port, &pp_ctx->port_info)) {
		std::cerr << "Failed to get port attributes" << std::endl;
		goto clean_pp_ctx;
	}
	// Set the local destination information
	for (int qp_idx = 0; qp_idx < num_qp; ++qp_idx) {
		local_dests[qp_idx]->lid = pp_ctx->port_info.lid;
		local_dests[qp_idx]->qpn = pp_ctx->qps[qp_idx]->qp_num;
		local_dests[qp_idx]->psn = std::uniform_int_distribution<uint32_t>(0, 0xffffff)(rng);
		if (pp_ctx->port_info.link_layer != IBV_LINK_LAYER_ETHERNET && !local_dests[qp_idx]->lid) {
			std::cerr << "Failed to get local LID with non-Ethernet link layer" << std::endl;
			goto clean_pp_ctx;
		}
		// Get the GID
		if (gid_idx >= 0) {
			if (ibv_query_gid(pp_ctx->dev_ctx, ib_port, gid_idx, &local_dests[qp_idx]->gid)) {
				std::cerr << "Failed to get local GID with index " << gid_idx << std::endl;
				goto clean_pp_ctx;
			}
		} else {
			memset(&local_dests[qp_idx]->gid, 0, sizeof(local_dests[qp_idx]->gid));
		}
		// Print the local destination information
		std::string wgid(INET6_ADDRSTRLEN, '\0');
		inet_ntop(AF_INET6, &local_dests[qp_idx]->gid, wgid.data(), wgid.size() - 1);
		std::cout << "local address for QP " << qp_idx << ":  LID 0x" << std::hex << std::setw(4) << std::setfill('0')
				  << local_dests[qp_idx]->lid << ", QPN 0x" << std::setw(6) << local_dests[qp_idx]->qpn << ", PSN 0x"
				  << std::setw(6) << local_dests[qp_idx]->psn << ", GID " << wgid << std::dec << std::setfill(' ')
				  << std::endl;
	}

	// Exchange remote destination by socket
	if (!server_name.empty()) {
		if (client_exch_dests(server_name, port, num_qp, local_dests, remote_dests)) {
			std::cerr << "Client: Failed to exchange remote destination information" << std::endl;
			goto clean_pp_ctx;
		}
	} else {
		if (server_exch_dests(pp_ctx, port, ib_port, mtu, sl, gid_idx, num_qp, local_dests, remote_dests)) {
			std::cerr << "Server: Failed to exchange remote destination information" << std::endl;
			goto clean_pp_ctx;
		}
	}

	// Print the remote destination information
	for (int qp_idx = 0; qp_idx < num_qp; ++qp_idx) {
		std::string wgid(INET6_ADDRSTRLEN, '\0');
		inet_ntop(AF_INET6, &remote_dests[qp_idx]->gid, wgid.data(), wgid.size() - 1);
		std::cout << "remote address for QP " << qp_idx << ":  LID 0x" << std::hex << std::setw(4) << std::setfill('0')
				  << remote_dests[qp_idx]->lid << ", QPN 0x" << std::setw(6) << remote_dests[qp_idx]->qpn << ", PSN 0x"
				  << std::setw(6) << remote_dests[qp_idx]->psn << ", GID " << wgid << std::dec << std::setfill(' ')
				  << std::endl;
	}

	// Set client QPs to RTS state
	if (!server_name.empty()) {
		if (modify_qp_state_rtr_rts(pp_ctx, ib_port, mtu, sl, gid_idx, num_qp, local_dests, remote_dests)) {
			std::cerr << "Failed to modify client QPs state to RTR/RTS" << std::endl;
			goto clean_pp_ctx;
		}
	}

	// client initializes buffer data and syncs data to server
	if (!server_name.empty()) {
		// Set the buffer to the validated data
		if (use_chk) {
			for (size_t i = 0; i < size; i += page_size) {
				pp_ctx->buf[i] = i / page_size % 128;
			}
		}
		// Post the send work request
		for (int qp_idx = 0; qp_idx < num_qp; ++qp_idx) {
			if (post_send_wr(pp_ctx, qp_idx)) {
				std::cerr << "QP " << qp_idx << " failed to post send work request" << std::endl;
				goto clean_pp_ctx;
			}
			pp_ctx->pending[qp_idx] |= PINGPONG_SEND_WRID;
		}
	}

	// Get the start time
	if (gettimeofday(&start, nullptr)) {
		std::cerr << "Failed to get start time" << std::endl;
		goto clean_pp_ctx;
	}

	// Send and receive the data until the number of iterations is reached
	rcnt = 0, scnt = 0;
	while (rcnt < iters || scnt < iters) {
		// Get the completion event
		if (use_event) {
			struct ibv_cq *ev_cq;
			void *ev_cq_ctx;
			// Get the CQ event
			if (ibv_get_cq_event(pp_ctx->event_channel, &ev_cq, &ev_cq_ctx)) {
				std::cerr << "Failed to get CQ event" << std::endl;
				goto clean_pp_ctx;
			}
			++num_cq_events;
			// Check the CQ
			if (ev_cq != pp_ctx->cq) {
				std::cerr << "CQ event for unknown CQ" << ev_cq << std::endl;
				goto clean_pp_ctx;
			}
			// Continously request the CQ notification
			if (ibv_req_notify_cq(pp_ctx->cq, 0)) {
				std::cerr << "Failed to request CQ notification" << std::endl;
				goto clean_pp_ctx;
			}
		}
		// Poll the work completions and post the next work request
		{
			int ne, ret;
			// Poll the completion queue
			do {
				ne = ibv_poll_cq(pp_ctx->cq, num_wc, wcs.data());
				if (ne < 0) {
					std::cerr << "Poll CQ failed: " << ne << std::endl;
					goto clean_pp_ctx;
				}
			} while (!use_event && ne < 1);
			// Process the work completions and post the next work request
			ret = process_wc(wcs, ne, pp_ctx, num_qp, &scnt, &rcnt, &routs, iters);
			if (ret) {
				std::cerr << "Process WC failed: " << ret << std::endl;
				goto clean_pp_ctx;
			}
		}
	}

	// Get the end time
	if (gettimeofday(&end, nullptr)) {
		std::cerr << "Failed to get end time" << std::endl;
		goto clean_pp_ctx;
	}

	// Process the results
	{
		// Print the statistics
		float usec = (end.tv_sec - start.tv_sec) * 1e6 + (end.tv_usec - start.tv_usec);
		long long bytes = (long long)size * iters * 2;
		const double seconds = usec / 1e6;
		const double mb_per_sec = bytes / usec;
		std::cout << std::fixed << std::setprecision(2);
		std::cout << bytes << " bytes in " << seconds << " seconds = " << mb_per_sec << " MB/s" << std::endl;
		std::cout << iters << " iters in " << seconds << " seconds = " << (usec / iters) << " us/iter" << std::endl;
		// Valid receive data from client
		if (server_name.empty() && use_chk) {
			for (size_t i = 0; i < size; i += page_size) {
				const unsigned char got = static_cast<unsigned char>(pp_ctx->buf[i]);
				const unsigned char expect = static_cast<unsigned char>((i / page_size) % 128);
				if (got != expect) {
					std::cout << "Invalid data at offset " << i << " (page " << i / page_size
							  << "): " << static_cast<unsigned int>(got) << " != " << static_cast<unsigned int>(expect)
							  << std::endl;
				}
			}
		}
	}

	// Ack CQ events
	if (use_event && num_cq_events) {
		ibv_ack_cq_events(pp_ctx->cq, num_cq_events);
	}

	// Close the pingpong context
	close_pp_ctx(pp_ctx, num_qp);

	// Free the IB device list
	ibv_free_device_list(dev_list);

	return 0;

clean_pp_ctx:
	close_pp_ctx(pp_ctx, num_qp);

clean_dev_list:
	ibv_free_device_list(dev_list);
	return 1;
}

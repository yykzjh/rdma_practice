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
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#include <asm-generic/errno-base.h>
#include <asm-generic/socket.h>
#include <bits/getopt_core.h>
#include <bits/getopt_ext.h>
#include <cstddef>
#include <infiniband/verbs.h>
#include <infiniband/verbs_api.h>
#include <ios>
#include <random>
#endif
#include <getopt.h>
#include <string>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <malloc.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <time.h>
#include <inttypes.h>
#include <iomanip>
#include <memory>
#include "include/pingpong.hpp"
#include "include/utils.hpp"

static int use_odp;
static int implicit_odp;
static int prefetch_mr;
static int use_ts;
static int validate_buf;
static int use_dm;
static int use_new_send;
static int page_size;

enum {
	PINGPONG_RECV_WRID = 1,
	PINGPONG_SEND_WRID = 2,
};

struct pingpong_context {
	struct ibv_context *context;
	struct ibv_comp_channel *channel;
	struct ibv_pd *pd;
	struct ibv_mr *mr;
	struct ibv_dm *dm;
	union {
		struct ibv_cq *cq;
		struct ibv_cq_ex *cq_ex;
	} cq_s;
	struct ibv_qp *qp;
	struct ibv_qp_ex *qpx;
	char *buf;
	int size;
	int send_flags;
	int rx_depth;
	int pending;
	struct ibv_port_attr port_info;
	uint64_t completion_timestamp_mask;
};

struct pingpong_dest {
	int lid;
	uint32_t qpn;
	uint32_t psn;
	union ibv_gid gid;
};

struct ts_params {
	double comp_recv_max_time_delta;
	double comp_recv_min_time_delta;
	double comp_recv_total_time_delta;
	double comp_recv_prev_time;
	int last_comp_with_ts;
	unsigned int comp_with_time_iters;
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
	std::cout << "  -r, --rx-depth=<dep>   number of receives to post at a time (default 500)" << std::endl;
	std::cout << "  -n, --iters=<iters>    number of exchanges (default 1000)" << std::endl;
	std::cout << "  -l, --sl=<sl>          service level value" << std::endl;
	std::cout << "  -e, --events           sleep on CQ events (default poll)" << std::endl;
	std::cout << "  -g, --gid-idx=<gid index> local port gid index" << std::endl;
	std::cout << "  -o, --odp		    use on demand paging" << std::endl;
	std::cout << "  -O, --iodp		    use implicit on demand paging" << std::endl;
	std::cout << "  -P, --prefetch	    prefetch an ODP MR" << std::endl;
	std::cout << "  -t, --ts	            get CQE with timestamp" << std::endl;
	std::cout << "  -c, --chk	            validate received buffer" << std::endl;
	std::cout << "  -j, --dm	            use device memory" << std::endl;
	std::cout << "  -N, --new_send            use new post send WR API" << std::endl;
}

static struct option long_options[] = {
	{ .name = "port", .has_arg = required_argument, .flag = nullptr, .val = 'p' },
	{ .name = "ib-dev", .has_arg = required_argument, .flag = nullptr, .val = 'd' },
	{ .name = "ib-port", .has_arg = required_argument, .flag = nullptr, .val = 'i' },
	{ .name = "size", .has_arg = required_argument, .flag = nullptr, .val = 's' },
	{ .name = "mtu", .has_arg = required_argument, .flag = nullptr, .val = 'm' },
	{ .name = "rx-depth", .has_arg = required_argument, .flag = nullptr, .val = 'r' },
	{ .name = "iters", .has_arg = required_argument, .flag = nullptr, .val = 'n' },
	{ .name = "sl", .has_arg = required_argument, .flag = nullptr, .val = 'l' },
	{ .name = "events", .has_arg = no_argument, .flag = nullptr, .val = 'e' },
	{ .name = "gid-idx", .has_arg = required_argument, .flag = nullptr, .val = 'g' },
	{ .name = "odp", .has_arg = no_argument, .flag = nullptr, .val = 'o' },
	{ .name = "iodp", .has_arg = no_argument, .flag = nullptr, .val = 'O' },
	{ .name = "prefetch", .has_arg = no_argument, .flag = nullptr, .val = 'P' },
	{ .name = "ts", .has_arg = no_argument, .flag = nullptr, .val = 't' },
	{ .name = "chk", .has_arg = no_argument, .flag = nullptr, .val = 'c' },
	{ .name = "dm", .has_arg = no_argument, .flag = nullptr, .val = 'j' },
	{ .name = "new_send", .has_arg = no_argument, .flag = nullptr, .val = 'N' },
	{}
};

static struct ibv_cq *pp_cq(struct pingpong_context *ctx)
{
	return use_ts ? ibv_cq_ex_to_cq(ctx->cq_s.cq_ex) : ctx->cq_s.cq;
}

static struct pingpong_context *init_pp_ctx(struct ibv_device *ib_dev, int size, int rx_depth, int port, int use_event)
{
	int access_flags = IBV_ACCESS_LOCAL_WRITE;

	// Allocate memory for the context
	struct pingpong_context *ctx = static_cast<struct pingpong_context *>(calloc(1, sizeof(*ctx)));
	if (!ctx)
		return nullptr;
	// Initialize the context
	ctx->size = size;
	ctx->send_flags = IBV_SEND_SIGNALED;
	ctx->rx_depth = rx_depth;

	// Allocate memory for the buffer
	ctx->buf = static_cast<char *>(memalign(page_size, size));
	if (!ctx->buf) {
		std::cerr << "Couldn't allocate work buffer" << std::endl;
		goto clean_ctx;
	}
	// Initialize the buffer
	memset(ctx->buf, 0x7b, size);

	// Open the device
	ctx->context = ibv_open_device(ib_dev);
	if (!ctx->context) {
		std::cerr << "Couldn't get context for " << ibv_get_device_name(ib_dev) << std::endl;
		goto clean_buffer;
	}

	// Create the completion channel
	if (use_event) {
		ctx->channel = ibv_create_comp_channel(ctx->context);
		if (!ctx->channel) {
			std::cerr << "Couldn't create completion channel" << std::endl;
			goto clean_device;
		}
	} else {
		ctx->channel = nullptr;
	}

	// Create the protection domain
	ctx->pd = ibv_alloc_pd(ctx->context);
	if (!ctx->pd) {
		std::cerr << "Couldn't allocate PD" << std::endl;
		goto clean_comp_channel;
	}

	// Check if the device supports the features
	if (use_odp || use_ts || use_dm) {
		const uint32_t rc_caps_mask = IBV_ODP_SUPPORT_SEND | IBV_ODP_SUPPORT_RECV;
		struct ibv_device_attr_ex dev_attr_ex;
		// Query the device attributes
		if (ibv_query_device_ex(ctx->context, NULL, &dev_attr_ex)) {
			std::cerr << "Couldn't query device for its features" << std::endl;
			goto clean_pd;
		}
		// Check if the device supports ODP
		if (use_odp) {
			if (!(dev_attr_ex.odp_caps.general_caps & IBV_ODP_SUPPORT) ||
			    (dev_attr_ex.odp_caps.per_transport_caps.rc_odp_caps & rc_caps_mask) != rc_caps_mask) {
				std::cerr
					<< "The device isn't ODP capable or doesn't support RC send and receive with ODP"
					<< std::endl;
				goto clean_pd;
			}
			if (implicit_odp && !(dev_attr_ex.odp_caps.general_caps & IBV_ODP_SUPPORT_IMPLICIT)) {
				std::cerr << "The device doesn't support implicit ODP" << std::endl;
				goto clean_pd;
			}
			access_flags |= IBV_ACCESS_ON_DEMAND;
		}
		// Check if the device supports timestamping
		if (use_ts) {
			if (!dev_attr_ex.completion_timestamp_mask) {
				std::cerr << "The device isn't completion timestamping capable" << std::endl;
				goto clean_pd;
			}
			ctx->completion_timestamp_mask = dev_attr_ex.completion_timestamp_mask;
		}
		// Check if the device supports device memory
		if (use_dm) {
			struct ibv_alloc_dm_attr dm_attr = {};
			if (!dev_attr_ex.max_dm_size) {
				std::cerr << "Device doesn't support dm allocation" << std::endl;
				goto clean_pd;
			}
			std::cout << "Max device memory size: " << dev_attr_ex.max_dm_size << " bytes" << std::endl;
			if (dev_attr_ex.max_dm_size < static_cast<uint64_t>(size)) {
				std::cerr << "Device memory is insufficient" << std::endl;
				goto clean_pd;
			}
			// Set the attributes for the DM allocation
			dm_attr.length = size;
			// Allocate the DM memory region
			ctx->dm = ibv_alloc_dm(ctx->context, &dm_attr);
			if (!ctx->dm) {
				std::cerr << "Device memory allocation failed" << std::endl;
				goto clean_pd;
			}
			access_flags |= IBV_ACCESS_ZERO_BASED;
		}
	}

	// Register the memory region
	if (implicit_odp) {
		ctx->mr = ibv_reg_mr(ctx->pd, NULL, SIZE_MAX, access_flags);
	} else {
		ctx->mr = use_dm ? ibv_reg_dm_mr(ctx->pd, ctx->dm, 0, size, access_flags) :
					 ibv_reg_mr(ctx->pd, ctx->buf, size, access_flags);
	}
	if (!ctx->mr) {
		std::cerr << "Couldn't register MR" << std::endl;
		goto clean_dm;
	}

	// Try to set the prefetch MR
	if (prefetch_mr) {
		struct ibv_sge sg_list;
		// Set the SGE list
		sg_list.lkey = ctx->mr->lkey;
		sg_list.addr = reinterpret_cast<uint64_t>(ctx->buf);
		sg_list.length = size;
		// Attempt to prefetch the MR
		int ret = ibv_advise_mr(ctx->pd, IBV_ADVISE_MR_ADVICE_PREFETCH_WRITE, IB_UVERBS_ADVISE_MR_FLAG_FLUSH,
					&sg_list, 1);
		if (ret) {
			std::cerr << "Couldn't prefetch MR(" << ret << "). Continue anyway" << std::endl;
		}
	}

	// Create the completion queue
	if (use_ts) {
		struct ibv_cq_init_attr_ex cq_init_attr_ex {
		};
		cq_init_attr_ex.cqe = static_cast<uint32_t>(rx_depth + 1);
		cq_init_attr_ex.cq_context = nullptr;
		cq_init_attr_ex.channel = ctx->channel;
		cq_init_attr_ex.comp_vector = 0;
		cq_init_attr_ex.wc_flags = IBV_WC_EX_WITH_COMPLETION_TIMESTAMP_WALLCLOCK;
		cq_init_attr_ex.comp_mask = 0;
		cq_init_attr_ex.flags = 0;
		cq_init_attr_ex.parent_domain = nullptr;
		ctx->cq_s.cq_ex = ibv_create_cq_ex(ctx->context, &cq_init_attr_ex);
	} else {
		ctx->cq_s.cq = ibv_create_cq(ctx->context, rx_depth + 1, nullptr, ctx->channel, 0);
	}
	// Check if the completion queue was created successfully
	if (!pp_cq(ctx)) {
		std::cerr << "Couldn't create CQ" << std::endl;
		goto clean_mr;
	}

	// Create the QP
	{
		// Set the QP init attributes for the old API
		struct ibv_qp_init_attr qp_init_attr {
		};
		qp_init_attr.send_cq = pp_cq(ctx);
		qp_init_attr.recv_cq = pp_cq(ctx);
		qp_init_attr.cap.max_send_wr = 1;
		qp_init_attr.cap.max_recv_wr = static_cast<uint32_t>(rx_depth);
		qp_init_attr.cap.max_send_sge = 1;
		qp_init_attr.cap.max_recv_sge = 1;
		qp_init_attr.cap.max_inline_data = 0;
		qp_init_attr.qp_type = IBV_QPT_RC;
		qp_init_attr.qp_context = nullptr;
		qp_init_attr.srq = nullptr;
		qp_init_attr.sq_sig_all = 0;
		// Set the QP init attributes for the new API
		if (use_new_send) {
			struct ibv_qp_init_attr_ex qp_init_attr_ex {
			};
			qp_init_attr_ex.send_cq = pp_cq(ctx);
			qp_init_attr_ex.recv_cq = pp_cq(ctx);
			qp_init_attr_ex.cap.max_send_wr = 1;
			qp_init_attr_ex.cap.max_recv_wr = static_cast<uint32_t>(rx_depth);
			qp_init_attr_ex.cap.max_send_sge = 1;
			qp_init_attr_ex.cap.max_recv_sge = 1;
			qp_init_attr_ex.cap.max_inline_data = 0;
			qp_init_attr_ex.qp_type = IBV_QPT_RC;
			qp_init_attr_ex.qp_context = nullptr;
			qp_init_attr_ex.srq = nullptr;
			qp_init_attr_ex.sq_sig_all = 0;
			qp_init_attr_ex.comp_mask = IBV_QP_INIT_ATTR_PD | IBV_QP_INIT_ATTR_SEND_OPS_FLAGS;
			qp_init_attr_ex.pd = ctx->pd;
			qp_init_attr_ex.send_ops_flags = IBV_QP_EX_WITH_SEND;
			qp_init_attr_ex.xrcd = nullptr;
			qp_init_attr_ex.create_flags = 0;
			qp_init_attr_ex.max_tso_header = 0;
			qp_init_attr_ex.rwq_ind_tbl = nullptr;
			qp_init_attr_ex.rx_hash_conf = {};
			qp_init_attr_ex.source_qpn = 0;
			// Create the QP with the new API
			ctx->qp = ibv_create_qp_ex(ctx->context, &qp_init_attr_ex);
		} else {
			// Create the QP with the old API
			ctx->qp = ibv_create_qp(ctx->pd, &qp_init_attr);
		}
		// Check if the QP was created successfully
		if (!ctx->qp) {
			std::cerr << "Create QP failed" << std::endl;
			goto clean_cq;
		}
		if (use_new_send) {
			// Get extended QP from QP
			ctx->qpx = ibv_qp_to_qp_ex(ctx->qp);
		}
		// Get QP attributes
		struct ibv_qp_attr qp_attr = {};
		ibv_query_qp(ctx->qp, &qp_attr, IBV_QP_CAP, &qp_init_attr);
		// Check if the inline data is supported
		if (qp_init_attr.cap.max_inline_data >= static_cast<uint32_t>(size) && !use_dm) {
			ctx->send_flags |= IBV_SEND_INLINE;
		}
	}

	// Modify the QP to INIT state
	{
		// Set the QP attributes
		struct ibv_qp_attr qp_attr {
		};
		qp_attr.qp_state = IBV_QPS_INIT;
		qp_attr.qp_access_flags = 0;
		qp_attr.pkey_index = 0;
		qp_attr.port_num = static_cast<uint8_t>(port);
		// Modify the QP to INIT state
		if (ibv_modify_qp(ctx->qp, &qp_attr,
				  IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)) {
			std::cerr << "Failed to modify QP to INIT state" << std::endl;
			goto clean_qp;
		}
	}

	return ctx;

clean_qp:
	ibv_destroy_qp(ctx->qp);

clean_cq:
	ibv_destroy_cq(pp_cq(ctx));

clean_mr:
	if (ctx->mr) {
		ibv_dereg_mr(ctx->mr);
	}

clean_dm:
	if (ctx->dm) {
		ibv_free_dm(ctx->dm);
	}

clean_pd:
	if (ctx->pd) {
		ibv_dealloc_pd(ctx->pd);
	}

clean_comp_channel:
	if (ctx->channel) {
		ibv_destroy_comp_channel(ctx->channel);
	}

clean_device:
	ibv_close_device(ctx->context);

clean_buffer:
	free(ctx->buf);

clean_ctx:
	free(ctx);

	return nullptr;
}

static int close_pp_ctx(struct pingpong_context *ctx)
{
	if (ibv_destroy_qp(ctx->qp)) {
		std::cerr << "Couldn't destroy QP" << std::endl;
		return 1;
	}

	if (ibv_destroy_cq(pp_cq(ctx))) {
		std::cerr << "Couldn't destroy CQ" << std::endl;
		return 1;
	}

	if (ibv_dereg_mr(ctx->mr)) {
		std::cerr << "Couldn't deregister MR" << std::endl;
		return 1;
	}

	if (ctx->dm) {
		if (ibv_free_dm(ctx->dm)) {
			std::cerr << "Couldn't free DM" << std::endl;
			return 1;
		}
	}

	if (ibv_dealloc_pd(ctx->pd)) {
		std::cerr << "Couldn't deallocate PD" << std::endl;
		return 1;
	}

	if (ctx->channel) {
		if (ibv_destroy_comp_channel(ctx->channel)) {
			std::cerr << "Couldn't destroy completion channel" << std::endl;
			return 1;
		}
	}

	if (ibv_close_device(ctx->context)) {
		std::cerr << "Couldn't release context" << std::endl;
		return 1;
	}

	free(ctx->buf);
	free(ctx);

	return 0;
}

static int post_recv_wr(struct pingpong_context *ctx, int n)
{
	// Initialize the SGE list
	struct ibv_sge sg_list = { .addr = use_dm ? 0 : reinterpret_cast<uint64_t>(ctx->buf),
				   .length = static_cast<uint32_t>(ctx->size),
				   .lkey = ctx->mr->lkey };
	// Initialize the receive work request
	struct ibv_recv_wr recv_wr {
	};
	recv_wr.wr_id = PINGPONG_RECV_WRID;
	recv_wr.sg_list = &sg_list;
	recv_wr.num_sge = 1;
	// Post n receive work requests
	struct ibv_recv_wr *bad_recv_wr = nullptr;
	for (int i = 0; i < n; ++i) {
		if (ibv_post_recv(ctx->qp, &recv_wr, &bad_recv_wr)) {
			return i;
		}
	}
	return n;
}

static int post_send_wr(struct pingpong_context *ctx)
{
	// Initialize the SGE list
	struct ibv_sge sg_list = {
		.addr = use_dm ? 0 : reinterpret_cast<uint64_t>(ctx->buf),
		.length = static_cast<uint32_t>(ctx->size),
		.lkey = ctx->mr->lkey,
	};
	// Initialize the send work request
	struct ibv_send_wr send_wr {
	};
	send_wr.wr_id = PINGPONG_SEND_WRID;
	send_wr.sg_list = &sg_list;
	send_wr.num_sge = 1;
	send_wr.opcode = IBV_WR_SEND;
	send_wr.send_flags = static_cast<unsigned int>(ctx->send_flags);
	struct ibv_send_wr *bad_send_wr = nullptr;
	if (use_new_send) {
		ibv_wr_start(ctx->qpx);
		ctx->qpx->wr_id = PINGPONG_SEND_WRID;
		ctx->qpx->wr_flags = ctx->send_flags;
		ibv_wr_send(ctx->qpx);
		ibv_wr_set_sge(ctx->qpx, sg_list.lkey, sg_list.addr, sg_list.length);
		return ibv_wr_complete(ctx->qpx);
	} else {
		return ibv_post_send(ctx->qp, &send_wr, &bad_send_wr);
	}
}

static std::shared_ptr<struct pingpong_dest> pp_client_exch_dest(const std::string &server_name, int port,
								 const struct pingpong_dest *my_dest)
{
	// Convert the port to a string
	std::string service;
	try {
		service = std::to_string(port);
	} catch (...) {
		std::cerr << "Failed to convert client port to string" << std::endl;
		return nullptr;
	}

	// Get the address information
	struct addrinfo *res = nullptr;
	struct addrinfo hints = {};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	int ret = getaddrinfo(server_name.c_str(), service.c_str(), &hints, &res);
	if (ret < 0) {
		std::cerr << gai_strerror(ret) << " for " << server_name << ":" << port << std::endl;
		return nullptr;
	}

	// Try to connect to the server
	int sockfd = -1;
	for (struct addrinfo *t = res; t; t = t->ai_next) {
		sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
		if (sockfd >= 0) {
			if (!connect(sockfd, t->ai_addr, t->ai_addrlen)) {
				break;
			}
			close(sockfd);
			sockfd = -1;
		}
	}
	// Free the address information
	freeaddrinfo(res);
	// Check if the connection was successful
	if (sockfd < 0) {
		std::cerr << "Failed to connect to " << server_name << ":" << port << std::endl;
		return nullptr;
	}

	// Convert the transferred data to string
	std::string wgid;
	gid_to_wire_gid(&my_dest->gid, wgid);
	std::ostringstream oss;
	oss << std::hex << std::nouppercase << std::setfill('0') << std::setw(4) << (my_dest->lid & 0xffff) << ':'
	    << std::setw(6) << (my_dest->qpn & 0xffffff) << ':' << std::setw(6) << (my_dest->psn & 0xffffff) << ':'
	    << wgid;
	std::string msg = oss.str();

	// Send the message to the server
	if (rdma_practice::write_all(sockfd, msg.c_str(), msg.size()) != static_cast<ssize_t>(msg.size())) {
		std::cerr << "Send local address failed" << std::endl;
		close(sockfd);
		return nullptr;
	}

	// Receive the message from the server
	if (rdma_practice::read_all(sockfd, msg.data(), msg.size()) != static_cast<ssize_t>(msg.size()) ||
	    rdma_practice::write_all(sockfd, "done", sizeof("done")) != sizeof("done")) {
		std::cerr << "Failed to read/write remote address" << std::endl;
		close(sockfd);
		return nullptr;
	}
	// Initialize the remote destination
	std::shared_ptr<struct pingpong_dest> rem_dest = std::make_shared<struct pingpong_dest>();
	// Parse the message
	std::replace(msg.begin(), msg.end(), ':', ' ');
	std::istringstream iss(msg);
	std::string remote_lid, remote_qpn, remote_psn, remote_wgid;
	iss >> remote_lid >> remote_qpn >> remote_psn >> remote_wgid;
	rem_dest->lid = std::stoi(remote_lid, nullptr, 16);
	rem_dest->qpn = std::stoul(remote_qpn, nullptr, 16);
	rem_dest->psn = std::stoul(remote_psn, nullptr, 16);
	wire_gid_to_gid(remote_wgid, &rem_dest->gid);

	return rem_dest;
}

static int pp_connect_ctx(struct pingpong_context *ctx, int ib_port, uint32_t my_psn, enum ibv_mtu mtu, int sl,
			  const std::shared_ptr<struct pingpong_dest> &remote_dest, int sgid_idx)
{
	// Initialize the QP state attributes
	struct ibv_qp_attr qp_attr {
	};
	qp_attr.qp_state = IBV_QPS_RTR;
	qp_attr.path_mtu = mtu;
	qp_attr.rq_psn = remote_dest->psn;
	qp_attr.dest_qp_num = remote_dest->qpn;
	qp_attr.ah_attr.dlid = static_cast<uint16_t>(remote_dest->lid);
	qp_attr.ah_attr.sl = static_cast<uint8_t>(sl);
	qp_attr.ah_attr.src_path_bits = 0;
	qp_attr.ah_attr.is_global = 0;
	qp_attr.ah_attr.port_num = static_cast<uint8_t>(ib_port);
	qp_attr.max_dest_rd_atomic = 1;
	qp_attr.min_rnr_timer = 12;
	// If the GID is global, set the global attributes
	if (remote_dest->gid.global.interface_id) {
		qp_attr.ah_attr.is_global = 1;
		qp_attr.ah_attr.grh.hop_limit = 1;
		qp_attr.ah_attr.grh.dgid = remote_dest->gid;
		qp_attr.ah_attr.grh.sgid_index = static_cast<uint8_t>(sgid_idx);
	}
	// Change the QP to RTR state
	if (ibv_modify_qp(ctx->qp, &qp_attr,
			  IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
				  IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER)) {
		std::cerr << "Failed to modify QP to RTR state" << std::endl;
		return 1;
	}
	// Change the QP to RTS state
	qp_attr.qp_state = IBV_QPS_RTS;
	qp_attr.timeout = 14;
	qp_attr.retry_cnt = 7;
	qp_attr.rnr_retry = 7;
	qp_attr.sq_psn = my_psn;
	qp_attr.max_rd_atomic = 1;
	if (ibv_modify_qp(ctx->qp, &qp_attr,
			  IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
				  IBV_QP_MAX_QP_RD_ATOMIC)) {
		std::cerr << "Failed to modify QP to RTS state" << std::endl;
		return 1;
	}

	return 0;
}

static std::shared_ptr<struct pingpong_dest> pp_server_exch_dest(struct pingpong_context *ctx, int ib_port,
								 enum ibv_mtu mtu, int port, int sl,
								 const struct pingpong_dest *my_dest, int sgid_idx)
{
	// Convert the port to a string
	std::string service;
	try {
		service = std::to_string(port);
	} catch (...) {
		std::cerr << "Failed to convert server port to string" << std::endl;
		return nullptr;
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
		return nullptr;
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
		return nullptr;
	}

	// Listen for connections
	ret = listen(sockfd, 1);
	if (ret < 0) {
		std::cerr << "Failed to listen for connection" << std::endl;
		close(sockfd);
		return nullptr;
	}

	// Accept one connection
	int connfd = accept(sockfd, nullptr, nullptr);
	// Close the listening socket
	close(sockfd);
	if (connfd < 0) {
		std::cerr << "accept() failed" << std::endl;
		return nullptr;
	}

	// Read the message from the client
	std::string msg(sizeof("0000:000000:000000:00000000000000000000000000000000"), '\0');
	ret = rdma_practice::read_all(connfd, msg.data(), msg.size());
	if (static_cast<size_t>(ret) != msg.size()) {
		perror("server read");
		std::cerr << ret << "/" << msg.size() << ": Failed to read message from client" << std::endl;
		close(connfd);
		return nullptr;
	}
	// Initialize the remote destination
	std::shared_ptr<struct pingpong_dest> rem_dest = std::make_shared<struct pingpong_dest>();
	// Parse the message
	std::replace(msg.begin(), msg.end(), ':', ' ');
	std::istringstream iss(msg);
	std::string remote_lid, remote_qpn, remote_psn, remote_wgid;
	iss >> remote_lid >> remote_qpn >> remote_psn >> remote_wgid;
	rem_dest->lid = std::stoi(remote_lid, nullptr, 16);
	rem_dest->qpn = std::stoul(remote_qpn, nullptr, 16);
	rem_dest->psn = std::stoul(remote_psn, nullptr, 16);
	wire_gid_to_gid(remote_wgid, &rem_dest->gid);

	// Connect to the remote QP
	if (pp_connect_ctx(ctx, ib_port, my_dest->psn, mtu, sl, rem_dest, sgid_idx)) {
		std::cerr << "Failed to connect to remote QP" << std::endl;
		close(connfd);
		return nullptr;
	}

	// Convert the transferred data to string
	std::string wgid;
	gid_to_wire_gid(&my_dest->gid, wgid);
	std::ostringstream oss;
	oss << std::hex << std::nouppercase << std::setfill('0') << std::setw(4) << (my_dest->lid & 0xffff) << ':'
	    << std::setw(6) << (my_dest->qpn & 0xffffff) << ':' << std::setw(6) << (my_dest->psn & 0xffffff) << ':'
	    << wgid;
	std::string send_msg = oss.str();
	// Send the message to the client
	if (write_all(connfd, send_msg.c_str(), send_msg.size()) !=
		    static_cast<ssize_t>(send_msg.size()) ||
	    read_all(connfd, msg.data(), sizeof("done")) != sizeof("done")) {
		std::cerr << "Failed to read/write remote address" << std::endl;
		close(connfd);
		return nullptr;
	}

	return rem_dest;
}

static inline int parse_single_wc(struct pingpong_context *ctx, size_t *scnt, size_t *rcnt, int *routs, size_t iters,
				  uint64_t wr_id, enum ibv_wc_status status, uint64_t completion_timestamp,
				  struct ts_params *ts)
{
	// Check the work completion status
	if (status != IBV_WC_SUCCESS) {
		std::cerr << "Failed status " << ibv_wc_status_str(status) << " (" << status << ") for wr_id" << wr_id
			  << std::endl;
		return 1;
	}
	// Parse the work completion according to the work request ID
	switch ((int)wr_id) {
	case PINGPONG_SEND_WRID: {
		++(*scnt);
		break;
	}
	case PINGPONG_RECV_WRID: {
		// If the number of receive work requests is less than or equal to 1, post more receive work requests
		if (--(*routs) <= 1) {
			*routs += post_recv_wr(ctx, ctx->rx_depth - *routs);
			if (*routs < ctx->rx_depth) {
				std::cerr << "Failed to post expected number of receive work requests: " << *routs
					  << "/" << ctx->rx_depth << std::endl;
				return 1;
			}
		}
		++(*rcnt);
		// Update the timestamp statistics
		if (use_ts) {
			// Not the first completion with timestamp
			if (ts->last_comp_with_ts) {
				double delta;
				// Checking whether the clock was wrapped around, calculate the delta between the current and previous completion timestamps
				if (completion_timestamp >= ts->comp_recv_prev_time) {
					delta = static_cast<double>(completion_timestamp - ts->comp_recv_prev_time);
				} else {
					delta = static_cast<double>(ctx->completion_timestamp_mask -
								    ts->comp_recv_prev_time + completion_timestamp + 1);
				}
				// Update the timestamp statistics
				ts->comp_recv_max_time_delta = std::max(ts->comp_recv_max_time_delta, delta);
				ts->comp_recv_min_time_delta = std::min(ts->comp_recv_min_time_delta, delta);
				ts->comp_recv_total_time_delta += delta;
				ts->comp_with_time_iters++;
			}
			// Update the previous completion timestamp
			ts->comp_recv_prev_time = completion_timestamp;
			ts->last_comp_with_ts = 1;
		} else {
			ts->last_comp_with_ts = 0;
		}
		break;
	}
	default: {
		std::cerr << "Work completion for unknown wr_id: " << wr_id << std::endl;
		return 1;
	}
	}
	// Clear the pending work request
	ctx->pending &= ~(int)wr_id;
	// Reset the pending work requests if the number of send and receive work requests is less than the number of iterations
	if (*scnt < iters && !ctx->pending) {
		if (post_send_wr(ctx)) {
			std::cerr << "Failed to post send work request" << std::endl;
			return 1;
		}
		ctx->pending |= PINGPONG_SEND_WRID | PINGPONG_RECV_WRID;
	}
	return 0;
}

int main(int argc, char **argv)
{
	// Initialize the variables
	unsigned int port = 18515;
	std::string ib_dev_name = "";
	int ib_port = 1;
	unsigned int size = 4096;
	enum ibv_mtu mtu = IBV_MTU_1024;
	unsigned int rx_depth = 500;
	size_t iters = 1000;
	int use_event = 0;
	int num_cq_events = 0;
	int sl = 0;
	int gidx = -1;
	struct ts_params ts;
	std::string server_name = "";
	// Initialize the random number generator
	std::mt19937 rng = make_rng();

	// Parse the command line arguments
	while (true) {
		// Parse one command line argument
		int opt_char = getopt_long(argc, argv, "p:d:i:s:m:r:n:l:eg:oOPtcjN", long_options, NULL);
		// No more options to parse
		if (opt_char == -1) {
			break;
		}
		// Handle the option
		switch (opt_char) {
		case 'p': {
			port = std::stoul(optarg);
			if (port > 65535) {
				usage(argv[0]);
				return 1;
			}
			break;
		}
		case 'd': {
			ib_dev_name = optarg;
			break;
		}
		case 'i': {
			ib_port = std::stoi(optarg);
			if (ib_port < 1) {
				usage(argv[0]);
				return 1;
			}
			break;
		}
		case 's': {
			size = std::stoul(optarg);
			break;
		}
		case 'm': {
			mtu = pp_mtu_to_enum(std::stoi(optarg));
			if (mtu == 0) {
				usage(argv[0]);
				return 1;
			}
			break;
		}
		case 'r': {
			rx_depth = std::stoul(optarg);
			break;
		}
		case 'n': {
			iters = static_cast<size_t>(std::stoul(optarg));
			break;
		}
		case 'l': {
			sl = std::stoi(optarg);
			break;
		}
		case 'e': {
			++use_event;
			break;
		}
		case 'g': {
			gidx = std::stoi(optarg);
			break;
		}
		case 'o': {
			use_odp = 1;
			break;
		}
		case 'P': {
			prefetch_mr = 1;
			break;
		}
		case 'O': {
			use_odp = 1;
			implicit_odp = 1;
			break;
		}
		case 't': {
			use_ts = 1;
			break;
		}
		case 'c': {
			validate_buf = 1;
			break;
		}
		case 'j': {
			use_dm = 1;
			break;
		}
		case 'N': {
			use_new_send = 1;
			break;
		}
		default: {
			usage(argv[0]);
			return 1;
		}
		}
	}

	// Check if the server name is provided
	if (optind == argc - 1) {
		server_name = argv[optind];
	} else if (optind < argc) {
		usage(argv[0]);
		return 1;
	}
	// Check if the DM memory region are used with ODP
	if (use_odp && use_dm) {
		std::cerr << "DM memory region cannot be used with ODP" << std::endl;
		return 1;
	}
	// Check if the prefetch MR is valid only with ODP
	if (!use_odp && prefetch_mr) {
		std::cerr << "Prefetch MR is valid only with on-demand paging memory region" << std::endl;
		return 1;
	}

	// Initialize the timestamp statistics
	if (use_ts) {
		ts.comp_recv_max_time_delta = 0;
		ts.comp_recv_min_time_delta = std::numeric_limits<double>::max();
		ts.comp_recv_total_time_delta = 0;
		ts.comp_recv_prev_time = 0;
		ts.last_comp_with_ts = 0;
		ts.comp_with_time_iters = 0;
	}

	// Get page size of the system
	page_size = sysconf(_SC_PAGESIZE);
	if (page_size == -1) {
		std::cerr << "Failed to get page size of the system" << std::endl;
		return 1;
	}

	// Get the IB device list
	struct ibv_device **dev_list = ibv_get_device_list(NULL);
	if (!dev_list) {
		std::cerr << "Failed to get IB devices list" << std::endl;
		return 1;
	}
	// Get the specific IB device
	struct ibv_device *ib_dev = nullptr;
	if (ib_dev_name.empty()) {
		std::cerr << "No IB device name provided" << std::endl;
		return 1;
	}
	for (int i = 0; dev_list[i]; ++i) {
		if (ib_dev_name == ibv_get_device_name(dev_list[i])) {
			ib_dev = dev_list[i];
			break;
		}
	}
	if (!ib_dev) {
		std::cerr << "IB device " << ib_dev_name << " not found" << std::endl;
		return 1;
	}

	// Initialize the pingpong context for specified device and port
	auto pp_ctx = init_pp_ctx(ib_dev, size, rx_depth, ib_port, use_event);
	if (!pp_ctx) {
		return 1;
	}

	// Post the receive work requests
	int routs = post_recv_wr(pp_ctx, pp_ctx->rx_depth);
	if (routs < pp_ctx->rx_depth) {
		std::cerr << "Failed to post " << pp_ctx->rx_depth << " receive work requests" << std::endl;
		return 1;
	}

	// Request CQ notification
	if (use_event) {
		if (ibv_req_notify_cq(pp_cq(pp_ctx), 0)) {
			std::cerr << "Failed to request CQ notification" << std::endl;
			return 1;
		}
	}

	// Get the port attributes
	if (pp_get_port_info(pp_ctx->context, ib_port, &pp_ctx->port_info)) {
		std::cerr << "Failed to get port attributes" << std::endl;
		return 1;
	}
	// Initialize the local identifier(LID)
	struct pingpong_dest my_dest {
	};
	my_dest.lid = pp_ctx->port_info.lid;
	if (pp_ctx->port_info.link_layer != IBV_LINK_LAYER_ETHERNET && !my_dest.lid) {
		std::cerr << "Failed to get LID" << std::endl;
		return 1;
	}
	// Get the GID
	if (gidx >= 0) {
		if (ibv_query_gid(pp_ctx->context, ib_port, gidx, &my_dest.gid)) {
			std::cerr << "Can't read sgid of index " << gidx << std::endl;
			return 1;
		}
	} else {
		memset(&my_dest.gid, 0, sizeof(my_dest.gid));
	}
	// Get the QPN and set the PSN randomly
	std::string lgid(33, '\0');
	my_dest.qpn = pp_ctx->qp->qp_num;
	my_dest.psn = std::uniform_int_distribution<uint32_t>(0, 0xffffff)(rng);
	inet_ntop(AF_INET6, &my_dest.gid, lgid.data(), lgid.size() - 1);
	std::cout << "local address:  LID 0x" << std::hex << std::setw(4) << std::setfill('0') << my_dest.lid
		  << ", QPN 0x" << std::setw(6) << my_dest.qpn << ", PSN 0x" << std::setw(6) << my_dest.psn << ", GID "
		  << lgid << std::dec << std::setfill(' ') << std::endl;

	// Exchange remote destination by socket
	std::shared_ptr<struct pingpong_dest> rem_dest;
	if (!server_name.empty()) {
		rem_dest = pp_client_exch_dest(server_name, port, &my_dest);
	} else {
		// Note: Server will change the QP to RTS state
		rem_dest = pp_server_exch_dest(pp_ctx, ib_port, mtu, port, sl, &my_dest, gidx);
	}
	if (!rem_dest) {
		std::cerr << "Failed to exchange remote destination" << std::endl;
		return 1;
	}
	// Print the remote destination
	std::string rgid(33, '\0');
	inet_ntop(AF_INET6, &rem_dest->gid, rgid.data(), rgid.size() - 1);
	std::cout << "remote address:  LID 0x" << std::hex << std::setw(4) << std::setfill('0') << rem_dest->lid
		  << ", QPN 0x" << std::setw(6) << rem_dest->qpn << ", PSN 0x" << std::setw(6) << rem_dest->psn
		  << ", GID " << rgid << std::dec << std::setfill(' ') << std::endl;

	// Change client QP to RTS state
	if (!server_name.empty()) {
		if (pp_connect_ctx(pp_ctx, ib_port, my_dest.psn, mtu, sl, rem_dest, gidx)) {
			std::cerr << "Failed to connect client QP to remote QP" << std::endl;
			return 1;
		}
	}
	pp_ctx->pending |= PINGPONG_RECV_WRID;

	// Client send the data to the server
	if (!server_name.empty()) {
		// Set the buffer to the validated data
		if (validate_buf) {
			for (size_t i = 0; i < size; i += page_size) {
				pp_ctx->buf[i] = i / page_size % 128;
			}
		}
		// Copy data from the buffer to the device memory
		if (use_dm) {
			if (ibv_memcpy_to_dm(pp_ctx->dm, 0, pp_ctx->buf, size)) {
				std::cerr << "Copy data from buffer to DM failed" << std::endl;
				return 1;
			}
		}
		// Post the send work request
		if (post_send_wr(pp_ctx)) {
			std::cerr << "Failed to post send work request" << std::endl;
			return 1;
		}
		pp_ctx->pending |= PINGPONG_SEND_WRID;
	}

	// Get the start time
	struct timeval start;
	if (gettimeofday(&start, nullptr)) {
		perror("gettimeofday");
		std::cerr << "Failed to get start time" << std::endl;
		return 1;
	}

	// Send and receive the data until the number of iterations is reached
	size_t rcnt = 0, scnt = 0;
	while (rcnt < iters || scnt < iters) {
		// Get the completion event
		if (use_event) {
			struct ibv_cq *ev_cq;
			void *ev_cq_ctx;
			// Get the CQ event
			if (ibv_get_cq_event(pp_ctx->channel, &ev_cq, &ev_cq_ctx)) {
				std::cerr << "Failed to get CQ event" << std::endl;
				return 1;
			}
			++num_cq_events;
			// Check the CQ
			if (ev_cq != pp_cq(pp_ctx)) {
				std::cerr << "CQ event for unknown CQ" << ev_cq << std::endl;
				return 1;
			}
			// Continously request the CQ notification
			if (ibv_req_notify_cq(pp_cq(pp_ctx), 0)) {
				std::cerr << "Failed to request CQ notification" << std::endl;
				return 1;
			}
		}
		// Poll the completion queue
		int ret = 0;
		if (use_ts) {
			struct ibv_poll_cq_attr poll_cq_attr;
			// Poll the completion queue until the completion event is received
			do {
				ret = ibv_start_poll(pp_ctx->cq_s.cq_ex, &poll_cq_attr);
			} while (ret == ENOENT);
			if (ret) {
				std::cerr << "Poll CQ failed: " << ret << std::endl;
				return 1;
			}
			// Parse the work completion
			ret = parse_single_wc(pp_ctx, &scnt, &rcnt, &routs, iters, pp_ctx->cq_s.cq_ex->wr_id,
					      pp_ctx->cq_s.cq_ex->status,
					      ibv_wc_read_completion_wallclock_ns(pp_ctx->cq_s.cq_ex), &ts);
			if (ret) {
				ibv_end_poll(pp_ctx->cq_s.cq_ex);
				return ret;
			}
			// Get the next work completion
			ret = ibv_next_poll(pp_ctx->cq_s.cq_ex);
			if (!ret) {
				ret = parse_single_wc(pp_ctx, &scnt, &rcnt, &routs, iters, pp_ctx->cq_s.cq_ex->wr_id,
						      pp_ctx->cq_s.cq_ex->status,
						      ibv_wc_read_completion_wallclock_ns(pp_ctx->cq_s.cq_ex), &ts);
			}
			// End the poll
			ibv_end_poll(pp_ctx->cq_s.cq_ex);
			if (ret && ret != ENOENT) {
				std::cerr << "Poll next work completion in CQ failed: " << ret << std::endl;
				return ret;
			}
		} else {
			int ne = 0;
			struct ibv_wc wc[2];
			// Poll the completion queue
			do {
				ne = ibv_poll_cq(pp_cq(pp_ctx), 2, wc);
				if (ne < 0) {
					std::cerr << "Poll CQ failed: " << ne << std::endl;
					return 1;
				}
			} while (ne < 1);
			// Parse the work completions
			for (int i = 0; i < ne; ++i) {
				ret = parse_single_wc(pp_ctx, &scnt, &rcnt, &routs, iters, wc[i].wr_id, wc[i].status, 0,
						      &ts);
				if (ret) {
					std::cerr << "Parse WC failed: " << ret << std::endl;
					return 1;
				}
			}
		}
	}

	// Get the end time
	struct timeval end;
	if (gettimeofday(&end, nullptr)) {
		perror("gettimeofday");
		std::cerr << "Failed to get end time" << std::endl;
		return 1;
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
		std::cout << iters << " iters in " << seconds << " seconds = " << (usec / iters) << " us/iter"
			  << std::endl;
		if (use_ts && ts.comp_with_time_iters) {
			std::cout << std::fixed << std::setprecision(4);
			std::cout << "Max receive completion interval (us) = " << ts.comp_recv_max_time_delta / 1000.0
				  << " us"
				  << "\n";
			std::cout << "Min receive completion interval (us) = " << ts.comp_recv_min_time_delta / 1000.0
				  << " us"
				  << "\n";
			std::cout << "Average receive completion interval (us) = "
				  << (static_cast<double>(ts.comp_recv_total_time_delta) / ts.comp_with_time_iters /
				      1000.0)
				  << " us"
				  << "\n";
		}
		std::cout.unsetf(std::ios::floatfield);
		// Valid receive data from client
		if (server_name.empty() && validate_buf) {
			if (use_dm) {
				if (ibv_memcpy_from_dm(pp_ctx->buf, pp_ctx->dm, 0, size)) {
					std::cerr << "Failed to copy data from DM to buffer" << std::endl;
					return 1;
				}
			}
			for (size_t i = 0; i < size; i += page_size) {
				const unsigned char got = static_cast<unsigned char>(pp_ctx->buf[i]);
				const unsigned char expect = static_cast<unsigned char>((i / page_size) % 128);
				if (got != expect) {
					std::cout << "Invalid data at offset " << i << " (page " << i / page_size
						  << "): " << static_cast<unsigned int>(got)
						  << " != " << static_cast<unsigned int>(expect) << std::endl;
				}
			}
		}
	}

	// Ack CQ events
	if (use_event && num_cq_events) {
		ibv_ack_cq_events(pp_cq(pp_ctx), num_cq_events);
	}

	// Close the pingpong context
	if (close_pp_ctx(pp_ctx)) {
		std::cerr << "Failed to close pingpong context" << std::endl;
		return 1;
	}

	// Free the IB device list
	ibv_free_device_list(dev_list);

	return 0;
}

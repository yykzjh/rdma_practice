#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cerrno>
#include <ios>
#include <iostream>
#include <getopt.h>
#include <string>
#include <iomanip>
#include <inttypes.h>
#include <arpa/inet.h>
#include <endian.h>
#include <fcntl.h>
#include <infiniband/verbs.h>
#include <unistd.h>

static bool verbose = false;

static void usage(const std::string &argv0)
{
	std::cout << "Usage: " << argv0
		  << "             print the ca attributes" << std::endl;
	std::cout << std::endl;
	std::cout << "Options:\n";
	std::cout
		<< "  -d, --ib-dev=<dev>     use IB device <dev> (default all devices)"
		<< std::endl;
	std::cout
		<< "  -i, --ib-port=<port>   use port <port> of IB device (default all ports)"
		<< std::endl;
	std::cout << "  -l, --list             print only the IB devices names"
		  << std::endl;
	std::cout
		<< "  -v, --verbose          print all the attributes of the IB device(s)"
		<< std::endl;
}

static std::string guid_str(std::uint64_t _node_guid)
{
	uint64_t node_guid = be64toh(_node_guid);

	char buf[20];
	std::snprintf(buf, sizeof(buf), "%04x:%04x:%04x:%04x",
		      (uint16_t)((node_guid >> 48) & 0xffff),
		      (uint16_t)((node_guid >> 32) & 0xffff),
		      (uint16_t)((node_guid >> 16) & 0xffff),
		      (uint16_t)(node_guid & 0xffff));
	return std::string(buf);
}

static std::string transport_str(enum ibv_transport_type transport)
{
	switch (transport) {
	case IBV_TRANSPORT_IB:
		return "InfiniBand";
	case IBV_TRANSPORT_IWARP:
		return "iWARP";
	case IBV_TRANSPORT_USNIC:
		return "usNIC";
	case IBV_TRANSPORT_USNIC_UDP:
		return "usNIC UDP";
	case IBV_TRANSPORT_UNSPECIFIED:
		return "unspecified";
	default:
		return "invalid transport";
	}
}

static std::string port_state_str(enum ibv_port_state pstate)
{
	switch (pstate) {
	case IBV_PORT_DOWN:
		return "PORT_DOWN";
	case IBV_PORT_INIT:
		return "PORT_INIT";
	case IBV_PORT_ARMED:
		return "PORT_ARMED";
	case IBV_PORT_ACTIVE:
		return "PORT_ACTIVE";
	default:
		return "invalid state";
	}
}

static std::string port_phy_state_str(uint8_t phys_state)
{
	switch (phys_state) {
	case 1:
		return "SLEEP";
	case 2:
		return "POLLING";
	case 3:
		return "DISABLED";
	case 4:
		return "PORT_CONFIGURATION TRAINING";
	case 5:
		return "LINK_UP";
	case 6:
		return "LINK_ERROR_RECOVERY";
	case 7:
		return "PHY TEST";
	default:
		return "invalid physical state";
	}
}

static std::string atomic_cap_str(enum ibv_atomic_cap atom_cap)
{
	switch (atom_cap) {
	case IBV_ATOMIC_NONE:
		return "ATOMIC_NONE";
	case IBV_ATOMIC_HCA:
		return "ATOMIC_HCA";
	case IBV_ATOMIC_GLOB:
		return "ATOMIC_GLOB";
	default:
		return "invalid atomic capability";
	}
}

static std::string mtu_str(enum ibv_mtu max_mtu)
{
	switch (max_mtu) {
	case IBV_MTU_256:
		return "256";
	case IBV_MTU_512:
		return "512";
	case IBV_MTU_1024:
		return "1024";
	case IBV_MTU_2048:
		return "2048";
	case IBV_MTU_4096:
		return "4096";
	default:
		return "invalid MTU";
	}
}

static std::string width_str(uint8_t width)
{
	switch (width) {
	case 1:
		return "1";
	case 2:
		return "2";
	case 4:
		return "4";
	case 8:
		return "8";
	case 16:
		return "16";
	default:
		return "invalid width";
	}
}

static std::string speed_str(uint32_t speed)
{
	switch (speed) {
	case 1:
		return "2.5 Gbps";
	case 2:
		return "5.0 Gbps";

	case 4: /*fall through*/
	case 8:
		return "10 Gbps";

	case 16:
		return "14.0 Gbps";
	case 32:
		return "25.0 Gbps";
	case 64:
		return "50.0 Gbps";
	case 128:
		return "100.0 Gbps";
	case 256:
		return "200.0 Gbps";
	default:
		return "invalid speed";
	}
}

static std::string vl_str(uint8_t vl_num)
{
	switch (vl_num) {
	case 1:
		return "1";
	case 2:
		return "2";
	case 3:
		return "4";
	case 4:
		return "8";
	case 5:
		return "15";
	default:
		return "invalid VL number";
	}
}

static int read_sysfs_file(const char *dir, const char *file, char *buf,
			   size_t size)
{
	if (!dir || !file || !buf || size == 0)
		return -1;

	std::string path(dir);
	path += "/";
	path += file;

	int fd = ::open(path.c_str(), O_RDONLY);
	if (fd < 0)
		return -1;

	ssize_t len = ::read(fd, buf, size - 1);
	int saved_errno = errno;
	::close(fd);
	errno = saved_errno;

	if (len <= 0)
		return static_cast<int>(len);

	buf[len] = '\0';
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		buf[--len] = '\0';

	return static_cast<int>(len);
}

enum class gid_type_sysfs {
	ib_roce_v1,
	roce_v2,
	invalid,
};

static const char *gid_type_str(gid_type_sysfs gid_type)
{
	switch (gid_type) {
	case gid_type_sysfs::ib_roce_v1:
		return "RoCE v1";
	case gid_type_sysfs::roce_v2:
		return "RoCE v2";
	default:
		return "invalid GID type";
	}
}

static int query_gid_type_sysfs(struct ibv_context *ctx, uint32_t port_num,
				int gid_index, gid_type_sysfs *out)
{
	if (!ctx || !ctx->device || !out)
		return -1;

	char buf[64] = {};
	const std::string file = std::string("ports/") +
				 std::to_string(port_num) +
				 "/gid_attrs/types/" +
				 std::to_string(gid_index);
	const int len = read_sysfs_file(ctx->device->ibdev_path, file.c_str(), buf,
					sizeof(buf));
	if (len <= 0)
		return -1;

	const std::string s(buf, static_cast<size_t>(len));
	// 常见内容: "IB/RoCE v1", "RoCE v1", "RoCE v2"
	if (s.find("RoCE v2") != std::string::npos) {
		*out = gid_type_sysfs::roce_v2;
		return 0;
	}
	if (s.find("RoCE v1") != std::string::npos ||
	    s.find("IB/RoCE v1") != std::string::npos ||
	    s == "IB") {
		*out = gid_type_sysfs::ib_roce_v1;
		return 0;
	}

	*out = gid_type_sysfs::invalid;
	return -1;
}

static void print_device_cap_flags(uint32_t dev_cap_flags)
{
	uint32_t unknown_flags = ~(
		IBV_DEVICE_RESIZE_MAX_WR | IBV_DEVICE_BAD_PKEY_CNTR |
		IBV_DEVICE_BAD_QKEY_CNTR | IBV_DEVICE_RAW_MULTI |
		IBV_DEVICE_AUTO_PATH_MIG | IBV_DEVICE_CHANGE_PHY_PORT |
		IBV_DEVICE_UD_AV_PORT_ENFORCE | IBV_DEVICE_CURR_QP_STATE_MOD |
		IBV_DEVICE_SHUTDOWN_PORT | IBV_DEVICE_INIT_TYPE |
		IBV_DEVICE_PORT_ACTIVE_EVENT | IBV_DEVICE_SYS_IMAGE_GUID |
		IBV_DEVICE_RC_RNR_NAK_GEN | IBV_DEVICE_SRQ_RESIZE |
		IBV_DEVICE_N_NOTIFY_CQ | IBV_DEVICE_MEM_WINDOW |
		IBV_DEVICE_UD_IP_CSUM | IBV_DEVICE_XRC |
		IBV_DEVICE_MEM_MGT_EXTENSIONS | IBV_DEVICE_MEM_WINDOW_TYPE_2A |
		IBV_DEVICE_MEM_WINDOW_TYPE_2B | IBV_DEVICE_RC_IP_CSUM |
		IBV_DEVICE_RAW_IP_CSUM | IBV_DEVICE_MANAGED_FLOW_STEERING);

	if (dev_cap_flags & IBV_DEVICE_RESIZE_MAX_WR)
		std::cout << "\t\t\t\t\tRESIZE_MAX_WR\n";
	if (dev_cap_flags & IBV_DEVICE_BAD_PKEY_CNTR)
		std::cout << "\t\t\t\t\tBAD_PKEY_CNTR\n";
	if (dev_cap_flags & IBV_DEVICE_BAD_QKEY_CNTR)
		std::cout << "\t\t\t\t\tBAD_QKEY_CNTR\n";
	if (dev_cap_flags & IBV_DEVICE_RAW_MULTI)
		std::cout << "\t\t\t\t\tRAW_MULTI\n";
	if (dev_cap_flags & IBV_DEVICE_AUTO_PATH_MIG)
		std::cout << "\t\t\t\t\tAUTO_PATH_MIG\n";
	if (dev_cap_flags & IBV_DEVICE_CHANGE_PHY_PORT)
		std::cout << "\t\t\t\t\tCHANGE_PHY_PORT\n";
	if (dev_cap_flags & IBV_DEVICE_UD_AV_PORT_ENFORCE)
		std::cout << "\t\t\t\t\tUD_AV_PORT_ENFORCE\n";
	if (dev_cap_flags & IBV_DEVICE_CURR_QP_STATE_MOD)
		std::cout << "\t\t\t\t\tCURR_QP_STATE_MOD\n";
	if (dev_cap_flags & IBV_DEVICE_SHUTDOWN_PORT)
		std::cout << "\t\t\t\t\tSHUTDOWN_PORT\n";
	if (dev_cap_flags & IBV_DEVICE_INIT_TYPE)
		std::cout << "\t\t\t\t\tINIT_TYPE\n";
	if (dev_cap_flags & IBV_DEVICE_PORT_ACTIVE_EVENT)
		std::cout << "\t\t\t\t\tPORT_ACTIVE_EVENT\n";
	if (dev_cap_flags & IBV_DEVICE_SYS_IMAGE_GUID)
		std::cout << "\t\t\t\t\tSYS_IMAGE_GUID\n";
	if (dev_cap_flags & IBV_DEVICE_RC_RNR_NAK_GEN)
		std::cout << "\t\t\t\t\tRC_RNR_NAK_GEN\n";
	if (dev_cap_flags & IBV_DEVICE_SRQ_RESIZE)
		std::cout << "\t\t\t\t\tSRQ_RESIZE\n";
	if (dev_cap_flags & IBV_DEVICE_N_NOTIFY_CQ)
		std::cout << "\t\t\t\t\tN_NOTIFY_CQ\n";
	if (dev_cap_flags & IBV_DEVICE_MEM_WINDOW)
		std::cout << "\t\t\t\t\tMEM_WINDOW\n";
	if (dev_cap_flags & IBV_DEVICE_UD_IP_CSUM)
		std::cout << "\t\t\t\t\tUD_IP_CSUM\n";
	if (dev_cap_flags & IBV_DEVICE_XRC)
		std::cout << "\t\t\t\t\tXRC\n";
	if (dev_cap_flags & IBV_DEVICE_MEM_MGT_EXTENSIONS)
		std::cout << "\t\t\t\t\tMEM_MGT_EXTENSIONS\n";
	if (dev_cap_flags & IBV_DEVICE_MEM_WINDOW_TYPE_2A)
		std::cout << "\t\t\t\t\tMEM_WINDOW_TYPE_2A\n";
	if (dev_cap_flags & IBV_DEVICE_MEM_WINDOW_TYPE_2B)
		std::cout << "\t\t\t\t\tMEM_WINDOW_TYPE_2B\n";
	if (dev_cap_flags & IBV_DEVICE_RC_IP_CSUM)
		std::cout << "\t\t\t\t\tRC_IP_CSUM\n";
	if (dev_cap_flags & IBV_DEVICE_RAW_IP_CSUM)
		std::cout << "\t\t\t\t\tRAW_IP_CSUM\n";
	if (dev_cap_flags & IBV_DEVICE_MANAGED_FLOW_STEERING)
		std::cout << "\t\t\t\t\tMANAGED_FLOW_STEERING\n";
	if (dev_cap_flags & unknown_flags)
		std::cout << "\t\t\t\t\tUnknown flags: 0x" << std::hex
			  << (dev_cap_flags & unknown_flags) << std::dec
			  << std::endl;
}

static void print_odp_trans_caps(uint32_t trans)
{
	uint32_t unknown_transport_caps =
		~(IBV_ODP_SUPPORT_SEND | IBV_ODP_SUPPORT_RECV |
		  IBV_ODP_SUPPORT_WRITE | IBV_ODP_SUPPORT_READ |
		  IBV_ODP_SUPPORT_ATOMIC | IBV_ODP_SUPPORT_SRQ_RECV);

	if (!trans) {
		std::cout << "\t\t\t\t\tNO SUPPORT\n";
	} else {
		if (trans & IBV_ODP_SUPPORT_SEND)
			std::cout << "\t\t\t\t\tSUPPORT_SEND\n";
		if (trans & IBV_ODP_SUPPORT_RECV)
			std::cout << "\t\t\t\t\tSUPPORT_RECV\n";
		if (trans & IBV_ODP_SUPPORT_WRITE)
			std::cout << "\t\t\t\t\tSUPPORT_WRITE\n";
		if (trans & IBV_ODP_SUPPORT_READ)
			std::cout << "\t\t\t\t\tSUPPORT_READ\n";
		if (trans & IBV_ODP_SUPPORT_ATOMIC)
			std::cout << "\t\t\t\t\tSUPPORT_ATOMIC\n";
		if (trans & IBV_ODP_SUPPORT_SRQ_RECV)
			std::cout << "\t\t\t\t\tSUPPORT_SRQ\n";
		if (trans & unknown_transport_caps)
			std::cout << "\t\t\t\t\tUnknown flags: 0x" << std::hex
				  << (trans & unknown_transport_caps)
				  << std::dec << std::endl;
	}
}

static void print_odp_caps(const struct ibv_device_attr_ex *device_attr)
{
	uint64_t unknown_general_caps =
		~(IBV_ODP_SUPPORT | IBV_ODP_SUPPORT_IMPLICIT);
	const struct ibv_odp_caps *caps = &device_attr->odp_caps;

	/* general odp caps */
	std::cout << "\tgeneral_odp_caps:\n";
	if (caps->general_caps & IBV_ODP_SUPPORT)
		std::cout << "\t\t\t\t\tODP_SUPPORT\n";
	if (caps->general_caps & IBV_ODP_SUPPORT_IMPLICIT)
		std::cout << "\t\t\t\t\tODP_SUPPORT_IMPLICIT\n";
	if (caps->general_caps & unknown_general_caps)
		std::cout << "\t\t\t\t\tUnknown flags: 0x" << std::hex
			  << (caps->general_caps & unknown_general_caps)
			  << std::dec << std::endl;

	/* RC transport */
	std::cout << "\trc_odp_caps:\n";
	print_odp_trans_caps(caps->per_transport_caps.rc_odp_caps);
	std::cout << "\tuc_odp_caps:\n";
	print_odp_trans_caps(caps->per_transport_caps.uc_odp_caps);
	std::cout << "\tud_odp_caps:\n";
	print_odp_trans_caps(caps->per_transport_caps.ud_odp_caps);
	std::cout << "\txrc_odp_caps:\n";
	print_odp_trans_caps(device_attr->xrc_odp_caps);
}

static void print_raw_packet_caps(uint32_t raw_packet_caps)
{
	std::cout << "\traw packet caps:\n";
	if (raw_packet_caps & IBV_RAW_PACKET_CAP_CVLAN_STRIPPING)
		std::cout << "\t\t\t\t\tC-VLAN stripping offload\n";
	if (raw_packet_caps & IBV_RAW_PACKET_CAP_SCATTER_FCS)
		std::cout << "\t\t\t\t\tScatter FCS offload\n";
	if (raw_packet_caps & IBV_RAW_PACKET_CAP_IP_CSUM)
		std::cout << "\t\t\t\t\tIP csum offload\n";
	if (raw_packet_caps & IBV_RAW_PACKET_CAP_DELAY_DROP)
		std::cout << "\t\t\t\t\tDelay drop\n";
}

static void print_device_cap_flags_ex(uint64_t device_cap_flags_ex)
{
	uint64_t ex_flags = device_cap_flags_ex & 0xffffffff00000000ULL;
	uint64_t unknown_flags = ~(IBV_DEVICE_RAW_SCATTER_FCS |
				   IBV_DEVICE_PCI_WRITE_END_PADDING);

	if (ex_flags & IBV_DEVICE_RAW_SCATTER_FCS)
		std::cout << "\t\t\t\t\tRAW_SCATTER_FCS\n";
	if (ex_flags & IBV_DEVICE_PCI_WRITE_END_PADDING)
		std::cout << "\t\t\t\t\tPCI_WRITE_END_PADDING\n";
	if (ex_flags & unknown_flags)
		std::cout << "\t\t\t\t\tUnknown flags: 0x" << std::hex
			  << (ex_flags & unknown_flags) << std::dec
			  << std::endl;
}

static void print_tso_caps(const struct ibv_tso_caps *caps)
{
	uint32_t unknown_general_caps =
		~(1 << IBV_QPT_RAW_PACKET | 1 << IBV_QPT_UD);
	std::cout << "\ttso_caps:\n";
	std::cout << "\t\tmax_tso:\t\t\t" << caps->max_tso << std::endl;

	if (caps->max_tso) {
		std::cout << "\t\tsupported_qp:\n";
		if (ibv_is_qpt_supported(caps->supported_qpts,
					 IBV_QPT_RAW_PACKET))
			std::cout << "\t\t\t\t\tSUPPORT_RAW_PACKET\n";
		if (ibv_is_qpt_supported(caps->supported_qpts, IBV_QPT_UD))
			std::cout << "\t\t\t\t\tSUPPORT_UD\n";
		if (caps->supported_qpts & unknown_general_caps)
			std::cout
				<< "\t\t\t\t\tUnknown flags: 0x" << std::hex
				<< (caps->supported_qpts & unknown_general_caps)
				<< std::dec << std::endl;
	}
}

static void print_rss_caps(const struct ibv_rss_caps *caps)
{
	uint32_t unknown_general_caps =
		~(1 << IBV_QPT_RAW_PACKET | 1 << IBV_QPT_UD);
	std::cout << "\trss_caps:\n";
	std::cout << "\t\tmax_rwq_indirection_tables:\t\t\t"
		  << caps->max_rwq_indirection_tables << std::endl;
	std::cout << "\t\tmax_rwq_indirection_table_size:\t\t\t"
		  << caps->max_rwq_indirection_table_size << std::endl;
	std::cout << "\t\trx_hash_function:\t\t\t\t0x" << std::hex
		  << caps->rx_hash_function << std::dec << std::endl;
	std::cout << "\t\trx_hash_fields_mask:\t\t\t\t0x" << std::hex
		  << caps->rx_hash_fields_mask << std::dec << std::endl;

	if (caps->supported_qpts) {
		std::cout << "\t\tsupported_qp:\n";
		if (ibv_is_qpt_supported(caps->supported_qpts,
					 IBV_QPT_RAW_PACKET))
			std::cout << "\t\t\t\t\tSUPPORT_RAW_PACKET\n";
		if (ibv_is_qpt_supported(caps->supported_qpts, IBV_QPT_UD))
			std::cout << "\t\t\t\t\tSUPPORT_UD\n";
		if (caps->supported_qpts & unknown_general_caps)
			std::cout
				<< "\t\t\t\t\tUnknown flags: 0x" << std::hex
				<< (caps->supported_qpts & unknown_general_caps)
				<< std::dec << std::endl;
	}
}

static void print_packet_pacing_caps(const struct ibv_packet_pacing_caps *caps)
{
	uint32_t unknown_general_caps =
		~(1 << IBV_QPT_RAW_PACKET | 1 << IBV_QPT_UD);
	std::cout << "\tpacket_pacing_caps:\n";
	std::cout << "\t\tqp_rate_limit_min:\t" << caps->qp_rate_limit_min
		  << "kbps" << std::endl;
	std::cout << "\t\tqp_rate_limit_max:\t" << caps->qp_rate_limit_max
		  << "kbps" << std::endl;

	if (caps->qp_rate_limit_max) {
		std::cout << "\t\tsupported_qp:\n";
		if (ibv_is_qpt_supported(caps->supported_qpts,
					 IBV_QPT_RAW_PACKET))
			std::cout << "\t\t\t\t\tSUPPORT_RAW_PACKET\n";
		if (ibv_is_qpt_supported(caps->supported_qpts, IBV_QPT_UD))
			std::cout << "\t\t\t\t\tSUPPORT_UD\n";
		if (caps->supported_qpts & unknown_general_caps)
			std::cout
				<< "\t\t\t\t\tUnknown flags: 0x" << std::hex
				<< (caps->supported_qpts & unknown_general_caps)
				<< std::dec << std::endl;
	}
}

static void print_tm_caps(const struct ibv_tm_caps *caps)
{
	if (caps->max_num_tags) {
		std::cout << "\tmax_rndv_hdr_size:\t\t"
			  << caps->max_rndv_hdr_size << std::endl;
		std::cout << "\tmax_num_tags:\t\t\t" << caps->max_num_tags
			  << std::endl;
		std::cout << "\tmax_ops:\t\t\t" << caps->max_ops << std::endl;
		std::cout << "\tmax_sge:\t\t\t" << caps->max_sge << std::endl;
		std::cout << "\tflags:\n";
		if (caps->flags & IBV_TM_CAP_RC)
			std::cout << "\t\t\t\t\tIBV_TM_CAP_RC\n";
	} else {
		std::cout << "\ttag matching not supported\n";
	}
}

static void
print_cq_moderation_caps(const struct ibv_cq_moderation_caps *cq_caps)
{
	if (!cq_caps->max_cq_count || !cq_caps->max_cq_period)
		return;

	std::cout << "\n\tcq moderation caps:\n";
	std::cout << "\t\tmax_cq_count:\t" << cq_caps->max_cq_count
		  << std::endl;
	std::cout << "\t\tmax_cq_period:\t" << cq_caps->max_cq_period << " us"
		  << std::endl;
}

static int null_gid(union ibv_gid *gid)
{
	return !(gid->raw[8] | gid->raw[9] | gid->raw[10] | gid->raw[11] |
		 gid->raw[12] | gid->raw[13] | gid->raw[14] | gid->raw[15]);
}

static void print_formated_gid(union ibv_gid *gid, int i,
			       gid_type_sysfs type, int ll)
{
	char gid_str[INET6_ADDRSTRLEN] = {};
	char str[20] = {};

	if (ll == IBV_LINK_LAYER_ETHERNET)
		std::snprintf(str, sizeof(str), ", %s", gid_type_str(type));

	if (type == gid_type_sysfs::ib_roce_v1)
		printf("\t\t\tGID[%3d]:\t\t%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x%s\n",
		       i, gid->raw[0], gid->raw[1], gid->raw[2], gid->raw[3],
		       gid->raw[4], gid->raw[5], gid->raw[6], gid->raw[7],
		       gid->raw[8], gid->raw[9], gid->raw[10], gid->raw[11],
		       gid->raw[12], gid->raw[13], gid->raw[14], gid->raw[15],
		       str);

	if (type == gid_type_sysfs::roce_v2) {
		inet_ntop(AF_INET6, gid->raw, gid_str, sizeof(gid_str));
		printf("\t\t\tGID[%3d]:\t\t%s%s\n", i, gid_str, str);
	}
}

static int print_all_port_gids(struct ibv_context *ctx,
			       struct ibv_port_attr *port_attr,
			       uint32_t port_num)
{
	gid_type_sysfs type = gid_type_sysfs::invalid;
	union ibv_gid gid;
	int tbl_len;
	int rc = 0;
	int i;

	tbl_len = port_attr->gid_tbl_len;
	for (i = 0; i < tbl_len; i++) {
		rc = ibv_query_gid(ctx, port_num, i, &gid);
		if (rc) {
			fprintf(stderr,
				"Failed to query gid to port %u, index %d\n",
				port_num, i);
			return rc;
		}

		if (query_gid_type_sysfs(ctx, port_num, i, &type))
			type = gid_type_sysfs::invalid;
		if (!null_gid(&gid))
			print_formated_gid(&gid, i, type,
					   port_attr->link_layer);
	}
	return rc;
}

static const char *link_layer_str(uint8_t link_layer)
{
	switch (link_layer) {
	case IBV_LINK_LAYER_UNSPECIFIED:
		return "Unspecified";
	case IBV_LINK_LAYER_INFINIBAND:
		return "InfiniBand";
	case IBV_LINK_LAYER_ETHERNET:
		return "Ethernet";
	default:
		return "Unknown";
	}
}

static int print_hca_cap(struct ibv_device *ib_dev, uint8_t ib_port)
{
	struct ibv_context *ctx;
	struct ibv_device_attr_ex device_attr = {};
	struct ibv_port_attr port_attr;
	int rc = 0;
	char buf[256];

	/* Get the context of the device */
	ctx = ibv_open_device(ib_dev);
	if (!ctx) {
		std::cerr << "Failed to open device" << std::endl;
		rc = 1;
		goto cleanup;
	}

	// Get extended device properties
	if (ibv_query_device_ex(ctx, NULL, &device_attr)) {
		std::cerr << "Failed to query device properties" << std::endl;
		rc = 2;
		goto cleanup;
	}

	// Check ib_port is valid
	if (ib_port && ib_port > device_attr.orig_attr.phys_port_cnt) {
		std::cerr << "Invalid port required for device" << std::endl;
		rc = 4;
		goto cleanup;
	}

	std::cout << "hca_id:\t" << ibv_get_device_name(ib_dev) << std::endl;
	std::cout << "\ttransport:\t\t\t"
		  << transport_str(ib_dev->transport_type) << " ("
		  << ib_dev->transport_type << ")" << std::endl;
	if (strlen(device_attr.orig_attr.fw_ver)) {
		std::cout << "\nfw_ver:\t\t\t\t" << device_attr.orig_attr.fw_ver
			  << std::endl;
	}
	std::cout << "\tnode_guid:\t\t\t"
		  << guid_str(device_attr.orig_attr.node_guid) << std::endl;
	std::cout << "\tsys_image_guid:\t\t\t"
		  << guid_str(device_attr.orig_attr.sys_image_guid)
		  << std::endl;
	std::cout << "\tvendor_id:\t\t\t0x" << std::hex << std::setw(4)
		  << std::setfill('0') << device_attr.orig_attr.vendor_id
		  << std::dec << std::setfill(' ') << std::endl;
	std::cout << "\tvendor_part_id:\t\t\t"
		  << device_attr.orig_attr.vendor_part_id << std::endl;
	std::cout << "\thw_ver:\t\t\t\t0x" << std::hex
		  << device_attr.orig_attr.hw_ver << std::dec << std::endl;
	if (read_sysfs_file(ib_dev->ibdev_path, "board_id", buf, sizeof(buf)) > 0) {
		std::cout << "\tboard_id:\t\t\t" << buf << std::endl;
	}
	std::cout << "\tphys_port_cnt:\t\t\t"
		  << device_attr.orig_attr.phys_port_cnt << std::endl;

	if (verbose) {
		std::cout << "\tmax_mr_size:\t\t\t0x" << std::hex
			  << device_attr.orig_attr.max_mr_size << std::dec
			  << std::endl;
		std::cout << "\tpage_size_cap:\t\t\t0x" << std::hex
			  << device_attr.orig_attr.page_size_cap << std::dec
			  << std::endl;
		std::cout << "\tmax_qp:\t\t\t\t" << device_attr.orig_attr.max_qp
			  << std::endl;
		std::cout << "\tmax_qp_wr:\t\t\t"
			  << device_attr.orig_attr.max_qp_wr << std::endl;
		std::cout << "\tdevice_cap_flags:\t\t0x" << std::hex
			  << std::setw(8) << std::setfill('0')
			  << device_attr.orig_attr.device_cap_flags << std::dec
			  << std::setfill(' ') << std::endl;
		print_device_cap_flags(device_attr.orig_attr.device_cap_flags);
		std::cout << "\tmax_sge:\t\t\t" << device_attr.orig_attr.max_sge
			  << std::endl;
		std::cout << "\tmax_sge_rd:\t\t\t"
			  << device_attr.orig_attr.max_sge_rd << std::endl;
		std::cout << "\tmax_cq:\t\t\t\t" << device_attr.orig_attr.max_cq
			  << std::endl;
		std::cout << "\tmax_cqe:\t\t\t" << device_attr.orig_attr.max_cqe
			  << std::endl;
		std::cout << "\tmax_mr:\t\t\t\t" << device_attr.orig_attr.max_mr
			  << std::endl;
		std::cout << "\tmax_pd:\t\t\t\t" << device_attr.orig_attr.max_pd
			  << std::endl;
		std::cout << "\tmax_qp_rd_atom:\t\t\t"
			  << device_attr.orig_attr.max_qp_rd_atom << std::endl;
		std::cout << "\tmax_ee_rd_atom:\t\t\t"
			  << device_attr.orig_attr.max_ee_rd_atom << std::endl;
		std::cout << "\tmax_res_rd_atom:\t\t\t"
			  << device_attr.orig_attr.max_res_rd_atom << std::endl;
		std::cout << "\tmax_qp_init_rd_atom:\t\t\t"
			  << device_attr.orig_attr.max_qp_init_rd_atom
			  << std::endl;
		std::cout << "\tmax_ee_init_rd_atom:\t\t\t"
			  << device_attr.orig_attr.max_ee_init_rd_atom
			  << std::endl;
		std::cout << "\tatomic_cap:\t\t\t"
			  << atomic_cap_str(device_attr.orig_attr.atomic_cap)
			  << " (" << device_attr.orig_attr.atomic_cap << ")"
			  << std::endl;
		std::cout << "\tmax_ee:\t\t\t\t" << device_attr.orig_attr.max_ee
			  << std::endl;
		std::cout << "\tmax_rdd:\t\t\t\t"
			  << device_attr.orig_attr.max_rdd << std::endl;
		std::cout << "\tmax_mw:\t\t\t\t" << device_attr.orig_attr.max_mw
			  << std::endl;
		std::cout << "\tmax_raw_ipv6_qp:\t\t"
			  << device_attr.orig_attr.max_raw_ipv6_qp << std::endl;
		std::cout << "\tmax_raw_ethy_qp:\t\t"
			  << device_attr.orig_attr.max_raw_ethy_qp << std::endl;
		std::cout << "\tmax_mcast_grp:\t\t\t"
			  << device_attr.orig_attr.max_mcast_grp << std::endl;
		std::cout << "\tmax_mcast_qp_attach:\t\t"
			  << device_attr.orig_attr.max_mcast_qp_attach
			  << std::endl;
		std::cout << "\tmax_total_mcast_qp_attach:\t"
			  << device_attr.orig_attr.max_total_mcast_qp_attach
			  << std::endl;
		std::cout << "\tmax_ah:\t\t\t\t" << device_attr.orig_attr.max_ah
			  << std::endl;
		std::cout << "\tmax_fmr:\t\t\t\t"
			  << device_attr.orig_attr.max_fmr << std::endl;
		if (device_attr.orig_attr.max_fmr > 0) {
			std::cout << "\tmax_map_per_fmr:\t\t"
				  << device_attr.orig_attr.max_map_per_fmr
				  << std::endl;
		}
		std::cout << "\tmax_srq:\t\t\t\t"
			  << device_attr.orig_attr.max_srq << std::endl;
		if (device_attr.orig_attr.max_srq > 0) {
			std::cout << "\tmax_srq_wr:\t\t\t"
				  << device_attr.orig_attr.max_srq_wr
				  << std::endl;
			std::cout << "\tmax_srq_sge:\t\t\t"
				  << device_attr.orig_attr.max_srq_sge
				  << std::endl;
		}
		std::cout << "\tmax_pkeys:\t\t\t"
			  << device_attr.orig_attr.max_pkeys << std::endl;
		std::cout << "\tlocal_ca_ack_delay:\t\t"
			  << device_attr.orig_attr.local_ca_ack_delay
			  << std::endl;
		print_odp_caps(&device_attr);
		if (device_attr.completion_timestamp_mask) {
			std::cout << "\tcompletion_timestamp_mask:\t\t\t0x"
				  << std::hex << std::setw(16)
				  << std::setfill('0')
				  << device_attr.completion_timestamp_mask
				  << std::dec << std::setfill(' ') << std::endl;
		} else {
			std::cout << "\tcompletion_timestamp_mask not supported"
				  << std::endl;
		}
		if (device_attr.hca_core_clock) {
			std::cout << "\thca_core_clock:\t\t\t"
				  << device_attr.hca_core_clock << "kHZ"
				  << std::endl;
		} else {
			std::cout << "\thca_core_clock not supported"
				  << std::endl;
		}
		if (device_attr.raw_packet_caps) {
			print_raw_packet_caps(device_attr.raw_packet_caps);
		}
		std::cout << "\tdevice_cap_flags_ex:\t\t0x" << std::hex
			  << device_attr.device_cap_flags_ex << std::dec
			  << std::endl;
		print_device_cap_flags_ex(device_attr.device_cap_flags_ex);
		print_tso_caps(&device_attr.tso_caps);
		print_rss_caps(&device_attr.rss_caps);
		std::cout << "\tmax_wq_type_rq:\t\t\t"
			  << device_attr.max_wq_type_rq << std::endl;
		print_packet_pacing_caps(&device_attr.packet_pacing_caps);
		print_tm_caps(&device_attr.tm_caps);
		print_cq_moderation_caps(&device_attr.cq_mod_caps);
		if (device_attr.max_dm_size) {
			std::cout << "\tmaximum available device memory:\t\t"
				  << device_attr.max_dm_size << " Bytes"
				  << std::endl;
		}
		std::cout << "\tnum_comp_vectors:\t\t" << ctx->num_comp_vectors
			  << std::endl;
	}

	for (uint32_t port = 1; port <= device_attr.orig_attr.phys_port_cnt;
	     ++port) {
		if (ib_port && port != ib_port)
			continue;

		rc = ibv_query_port(ctx, port, &port_attr);
		if (rc) {
			std::cerr << "Failed to query port " << port << " props"
				  << std::endl;
			goto cleanup;
		}

		std::cout << "\t\tport:\t" << port << std::endl;
		std::cout << "\t\t\tstate:\t\t\t"
			  << port_state_str(port_attr.state) << " ("
			  << port_attr.state << ")" << std::endl;
		std::cout << "\t\t\tmax_mtu:\t\t" << mtu_str(port_attr.max_mtu)
			  << " (" << port_attr.max_mtu << ")" << std::endl;
		std::cout << "\t\t\tactive_mtu:\t\t"
			  << mtu_str(port_attr.active_mtu) << " ("
			  << port_attr.active_mtu << ")" << std::endl;
		std::cout << "\t\t\tsm_lid:\t\t\t" << port_attr.sm_lid
			  << std::endl;
		std::cout << "\t\t\tport_lid:\t\t" << port_attr.lid
			  << std::endl;
		std::cout << "\t\t\tport_lmc:\t\t0x" << std::hex << std::setw(2)
			  << std::setfill('0') << port_attr.lmc << std::dec
			  << std::setfill(' ') << std::endl;
		std::cout << "\t\t\tlink_layer:\t\t"
			  << link_layer_str(port_attr.link_layer) << std::endl;

		if (verbose) {
			std::cout << "\t\t\tmax_msg_sz:\t\t0x" << std::hex
				  << port_attr.max_msg_sz << std::dec
				  << std::endl;
			std::cout << "\t\t\tport_cap_flags:\t\t0x" << std::hex
				  << std::setw(8) << std::setfill('0')
				  << port_attr.port_cap_flags << std::dec
				  << std::setfill(' ') << std::endl;
			std::cout << "\t\t\tport_cap_flags2:\t0x" << std::hex
				  << std::setw(4) << std::setfill('0')
				  << port_attr.port_cap_flags2 << std::dec
				  << std::setfill(' ') << std::endl;
			std::cout << "\t\t\tmax_vl_num:\t\t"
				  << vl_str(port_attr.max_vl_num) << " ("
				  << port_attr.max_vl_num << ")" << std::endl;
			std::cout << "\t\t\tbad_pkey_cntr:\t\t0x" << std::hex
				  << port_attr.bad_pkey_cntr << std::dec
				  << std::endl;
			std::cout << "\t\t\tqkey_viol_cntr:\t\t0x" << std::hex
				  << port_attr.qkey_viol_cntr << std::dec
				  << std::endl;
			std::cout << "\t\t\tsm_sl:\t\t\t" << port_attr.sm_sl
				  << std::endl;
			std::cout << "\t\t\tpkey_tbl_len:\t\t"
				  << port_attr.pkey_tbl_len << std::endl;
			std::cout << "\t\t\tgid_tbl_len:\t\t"
				  << port_attr.gid_tbl_len << std::endl;
			std::cout << "\t\t\tsubnet_timeout:\t\t"
				  << port_attr.subnet_timeout << std::endl;
			std::cout << "\t\t\tinit_type_reply:\t"
				  << port_attr.init_type_reply << std::endl;
			std::cout << "\t\t\tactive_width:\t\t"
				  << width_str(port_attr.active_width) << "X ("
				  << port_attr.active_width << ")" << std::endl;
			std::cout
				<< "\t\t\tactive_speed:\t\t"
				<< speed_str(port_attr.active_speed)
				<< " ("
				<< port_attr.active_speed
				<< ")" << std::endl;
			if (ib_dev->transport_type == IBV_TRANSPORT_IB)
				std::cout << "\t\t\tphys_state:\t\t"
					  << port_phy_state_str(
						     port_attr.phys_state)
					  << " (" << port_attr.phys_state << ")"
					  << std::endl;

			rc = print_all_port_gids(ctx, &port_attr, port);
			if (rc)
				goto cleanup;
		}
        std::cout << std::endl;
	}

cleanup:
	if (ctx) {
		if (ibv_close_device(ctx) != 0) {
			std::cerr << "Failed to close device" << std::endl;
			rc = 3;
		}
	}
	return rc;
}

int main(int argc, char **argv)
{
	std::string ib_dev_name;
	int ib_port = 0, ret = 0;

	// Parse command line arguments
	while (true) {
		static struct option long_options[] = {
			{ .name = "ib_dev",
			  .has_arg = required_argument,
			  .flag = nullptr,
			  .val = 'd' },
			{ .name = "ib_port",
			  .has_arg = required_argument,
			  .flag = nullptr,
			  .val = 'p' },
			{ .name = "list",
			  .has_arg = no_argument,
			  .flag = nullptr,
			  .val = 'l' },
			{ .name = "verbose",
			  .has_arg = no_argument,
			  .flag = nullptr,
			  .val = 'v' },
			{}
		};

		int opt_char =
			getopt_long(argc, argv, "d:p:lv", long_options, NULL);
		if (opt_char == -1)
			break;

		switch (opt_char) {
		case 'd': {
			ib_dev_name = optarg;
			break;
		}
		case 'p': {
			ib_port = std::stoi(optarg);
			if (ib_port <= 0) {
				usage(argv[0]);
				return 1;
			}
			break;
		}
		case 'l': {
			int num_devices = 0;
			struct ibv_device **devices =
				ibv_get_device_list(&num_devices);
			if (!devices) {
				std::cerr << "Failed to get IB devices list"
					  << std::endl;
				return 1;
			}
			std::cout << num_devices
				  << " HCA(s) found:" << std::endl;
			struct ibv_device **current_device = devices;
			while (*current_device) {
				std::cout << "\t" << (*current_device)->name;
				++current_device;
			}
			std::cout << std::endl;
			ibv_free_device_list(devices);
			return 0;
		}
		case 'v': {
			verbose = true;
			break;
		}
		default: {
			usage(argv[0]);
			return 1;
		}
		}
	}

	int num_devices = 0;
	struct ibv_device **cur_dev = nullptr, **origin_dev_list = nullptr;
	cur_dev = origin_dev_list = ibv_get_device_list(&num_devices);
	// Get device list
	if (!cur_dev) {
		std::cerr << "Failed to get IB devices list" << std::endl;
		return 1;
	}

	if (!ib_dev_name.empty()) {
		// Filter devices by name if specified
		while (*cur_dev) {
			if ((*cur_dev)->name == ib_dev_name) {
				break;
			}
			cur_dev++;
		}
		// Check if the device was found
		if (!*cur_dev) {
			std::cerr << "IB device " << ib_dev_name
				  << " was not found" << std::endl;
			ret = 1;
			goto out;
		}
        // Print HCA capabilities
        ret |= print_hca_cap(*cur_dev, ib_port);
	} else {
        // No IB devices found
        if (!*cur_dev) {
            std::cerr << "No IB devices found" << std::endl;
            ret = 1;
            goto out;
        }
        // Print HCA capabilities for each device
        while (*cur_dev) {
            ret |= print_hca_cap(*cur_dev, ib_port);
            cur_dev++;
        }
    }

out:
	ibv_free_device_list(origin_dev_list);

	return ret;
}

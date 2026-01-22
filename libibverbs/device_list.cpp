#include <iostream>
#include <iomanip>
#include <endian.h>
#include <infiniband/verbs.h>

int main(int argc, char *argv[])
{
	// Suppress unused parameter warnings
	(void)argc;
	(void)argv;

	int num_devices = 0;
	// Get the list of IB devices
	struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list) {
		std::cerr << "Failed to get IB devices list" << std::endl;
		return 1;
	}

	std::cout << "    " << std::left << std::setw(16) << "device"
		  << "\t   node GUID\n";
	std::cout << "    " << std::left << std::setw(16) << "------"
		  << "\t----------------\n";

	for (int i = 0; i < num_devices; ++i) {
		const char *name = ibv_get_device_name(dev_list[i]);
		unsigned long long guid = (unsigned long long)be64toh(
			ibv_get_device_guid(dev_list[i]));

		std::cout << "    " << std::left << std::setw(16)
			  << (name ? name : "(null)") << "\t" << std::right
			  << std::hex << std::setw(16) << std::setfill('0')
			  << guid << std::dec << std::setfill(' ') << "\n";
	}

	for (int i = 0; i < num_devices; ++i) {
		std::cout << dev_list[i]->name << "    "
			  << dev_list[i]->ibdev_path << "    "
			  << dev_list[i]->dev_name << "    "
			  << dev_list[i]->dev_path
			  << "    ibv_node_type: " << dev_list[i]->node_type
			  << "    ibv_transport_type: "
			  << dev_list[i]->transport_type << std::endl;
	}

	ibv_free_device_list(dev_list);
	return 0;
}

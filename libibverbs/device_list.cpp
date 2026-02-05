/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
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

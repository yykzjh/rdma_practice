/*
 * Copyright (c) 2006 Cisco Systems.  All rights reserved.
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

#include "include/pingpong.hpp"
#include <endian.h>
#include <iostream>
#include <string>
#include <array>
#include <charconv>

enum ibv_mtu pp_mtu_to_enum(int mtu)
{
	switch (mtu) {
	case 256:
		return IBV_MTU_256;
	case 512:
		return IBV_MTU_512;
	case 1024:
		return IBV_MTU_1024;
	case 2048:
		return IBV_MTU_2048;
	case 4096:
		return IBV_MTU_4096;
	default:
		return static_cast<ibv_mtu>(0);
	}
}

int pp_get_port_info(struct ibv_context *context, int port,
		     struct ibv_port_attr *attr)
{
	return ibv_query_port(context, port, attr);
}

void wire_gid_to_gid(std::string &wgid, union ibv_gid *gid)
{
	if (!gid) {
		throw std::invalid_argument("gid is null");
	}
	if (wgid.size() < 32) {
		throw std::invalid_argument(
			"wgid must have at least 32 hex characters");
	}

	std::array<std::uint32_t, 4> tmp_gid{};

	for (std::size_t i = 0; i < 4; ++i) {
		const char *first = wgid.data() + i * 8;
		const char *last = first + 8;

		std::uint32_t v32_be = 0;
		const auto res = std::from_chars(first, last, v32_be, 16);
		if (res.ec != std::errc{} || res.ptr != last) {
			throw std::invalid_argument(
				"wgid contains invalid hex characters");
		}
		tmp_gid[i] = be32toh(v32_be);
	}

	memcpy(gid, tmp_gid.data(), sizeof(*gid));
}

void gid_to_wire_gid(const union ibv_gid *gid, std::string &wgid)
{
	if (!gid) {
		wgid.clear();
		return;
	}

	std::array<std::uint32_t, 4> tmp{};
	memcpy(tmp.data(), gid, sizeof(tmp));

	wgid.resize(33);

	for (std::size_t i = 0; i < 4; ++i) {
		std::uint32_t v = htobe32(tmp[i]);
		std::snprintf(&wgid[i * 8], 9, "%08x", v);
	}
}
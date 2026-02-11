#include <chrono>
#include <cstdint>
#include <random>
#include <unistd.h>

#include "include/utils.hpp"

namespace rdma_practice {

std::mt19937 make_rng()
{
	std::random_device rd;

	const auto now = static_cast<std::uint64_t>(
		std::chrono::high_resolution_clock::now()
			.time_since_epoch()
			.count());

	const std::uint64_t pid = static_cast<std::uint64_t>(::getpid());

	std::seed_seq seed{
		rd(),
		rd(),
		rd(),
		rd(),
		static_cast<std::uint32_t>(now),
		static_cast<std::uint32_t>(now >> 32),
		static_cast<std::uint32_t>(pid),
		static_cast<std::uint32_t>(pid >> 32),
	};

	return std::mt19937(seed);
}

ssize_t read_all(int fd, void *buf, size_t n)
{
	char *p = static_cast<char *>(buf);
	size_t left = n;

	while (left > 0) {
		ssize_t r = ::read(fd, p, left);
		if (r > 0) {
			p += r;
			left -= static_cast<size_t>(r);
			continue;
		}
		if (r == 0) {
			// The remote side closed the connection (EOF), so we can't read the full amount
			return static_cast<ssize_t>(n - left);
		}
		return r;
	}
	return static_cast<ssize_t>(n);
}

ssize_t write_all(int fd, const void *buf, size_t n)
{
	const char *p = static_cast<const char *>(buf);
	size_t left = n;

	while (left > 0) {
		ssize_t w = ::write(fd, p, left);
		if (w > 0) {
			p += w;
			left -= static_cast<size_t>(w);
			continue;
		}
		if (w == 0) {
			// write returns 0: usually means that it's not possible to continue (for pipes/special cases), so treat it as a failure
			return static_cast<ssize_t>(n - left);
		}
		return w;
	}
	return static_cast<ssize_t>(n);
}

}
#include <chrono>
#include <cstdint>
#include <random>
#include <unistd.h>

#include "include/utils.hpp"

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

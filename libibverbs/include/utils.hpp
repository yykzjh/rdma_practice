#pragma once
#include <random>

namespace rdma_practice {

std::mt19937 make_rng();

ssize_t read_all(int fd, void *buf, size_t n);
ssize_t write_all(int fd, const void *buf, size_t n);
}


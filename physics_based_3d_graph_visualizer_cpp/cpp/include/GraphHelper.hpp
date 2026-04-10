#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace graph {

std::uint64_t EdgeKey(int a, int b);
std::vector<std::vector<int>> MatrixToAdjacencyList(const std::vector<std::vector<int>>& adjacency_matrix);

std::vector<std::vector<int>> EdgesToAdjacencyList(const std::vector<std::pair<int, int>>& edges,
                                                   std::size_t node_count = 0);

}  // namespace graph

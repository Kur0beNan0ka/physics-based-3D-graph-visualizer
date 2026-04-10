#include "GraphHelper.hpp"

#include <algorithm>

namespace graph {

std::uint64_t EdgeKey(int a, int b) {
    const std::uint32_t u = static_cast<std::uint32_t>(std::min(a, b));
    const std::uint32_t v = static_cast<std::uint32_t>(std::max(a, b));
    return (static_cast<std::uint64_t>(u) << 32U) | static_cast<std::uint64_t>(v);
}

std::vector<std::vector<int>> MatrixToAdjacencyList(const std::vector<std::vector<int>>& adjacency_matrix) {
    std::vector<std::vector<int>> adjacency(adjacency_matrix.size());
    for (std::size_t i = 0; i < adjacency_matrix.size(); ++i) {
        for (std::size_t j = 0; j < adjacency_matrix[i].size(); ++j) {
            if (adjacency_matrix[i][j] != 0) {
                adjacency[i].push_back(static_cast<int>(j));
            }
        }
    }
    return adjacency;
}

std::vector<std::vector<int>> EdgesToAdjacencyList(const std::vector<std::pair<int, int>>& edges,
                                                   std::size_t node_count) {
    std::size_t inferred_count = node_count;
    for (const auto& edge : edges) {
        inferred_count = std::max(inferred_count, static_cast<std::size_t>(std::max(edge.first, edge.second) + 1));
    }

    std::vector<std::vector<int>> adjacency(inferred_count);
    for (const auto& edge : edges) {
        adjacency[static_cast<std::size_t>(edge.first)].push_back(edge.second);
        adjacency[static_cast<std::size_t>(edge.second)].push_back(edge.first);
    }
    return adjacency;
}

}  // namespace graph

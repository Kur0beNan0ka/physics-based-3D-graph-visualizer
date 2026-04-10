#pragma once

#include "GraphCommon.hpp"
#include "Klotski.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <random>
#include <unordered_set>
#include <utility>
#include <vector>

namespace graph {

class BfsGrowthSimulation3D {
  public:
    struct Parameters {
        float k = 0.05f;
        float gravity = 0.01f;
        float damping = 0.95f;
        float dt = 0.05f;
        float intensity = 4.0f;
        float repulsion_theta = 0.78f;
        float repulsion_softening = 1e-4f;
        float max_velocity = 10.0f;
        int worker_threads = 0;
    };

    explicit BfsGrowthSimulation3D(const PuzzleGraphBuilder::GraphData& graph,
                                   int root_node,
                                   Parameters params);

    [[nodiscard]] std::size_t TotalNodeCount() const;
    [[nodiscard]] std::size_t ActiveNodeCount() const;
    [[nodiscard]] std::size_t ActiveEdgeCount() const;
    [[nodiscard]] bool GrowthFinished() const;
    [[nodiscard]] const std::vector<unsigned int>& EdgeIndices() const;
    [[nodiscard]] float MaxVelocityMagnitude() const;

    bool ConsumeEdgeIndexBufferDirty();
    bool AddNextBfsNode();
    void StepPhysics(int iterations);
    void BuildPointBuffer(std::vector<float>& point_vertices, float scene_scale = 1.0f) const;

  private:
    class BarnesHutSolver;

    const PuzzleGraphBuilder::GraphData& graph_;
    Parameters params_;
    std::vector<Vec3> pos_;
    std::vector<Vec3> vel_;
    std::vector<Vec3> force_;
    std::vector<int> dense_index_;
    std::vector<std::uint8_t> active_;
    std::vector<std::uint8_t> discovered_;
    std::vector<int> active_nodes_;
    std::deque<int> bfs_queue_;
    std::vector<std::pair<int, int>> active_edges_;
    std::vector<unsigned int> edge_indices_;
    std::unordered_set<std::uint64_t> active_edge_set_;
    bool edge_index_buffer_dirty_ = true;
    std::mt19937 rng_;

    void ActivateRoot(int root);
    Vec3 RandomUnitVector();
};

}  // namespace graph

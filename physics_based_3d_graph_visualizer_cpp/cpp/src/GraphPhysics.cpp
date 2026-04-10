#include "GraphPhysics.hpp"

#include "GraphHelper.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <thread>

namespace graph {

class BfsGrowthSimulation3D::BarnesHutSolver {
  public:
    BarnesHutSolver(const std::vector<Vec3>& positions,
                    const std::vector<int>& active_nodes,
                    float k_sq,
                    float theta,
                    float softening)
        : positions_(positions),
          active_nodes_(active_nodes),
          k_sq_(k_sq),
          theta_(theta),
          softening_(softening) {
        BuildTree();
    }

    Vec3 ComputeRepulsion(int node_index) const {
        Vec3 force{};
        if (nodes_.empty()) {
            return force;
        }
        Accumulate(0, node_index, force);
        return force;
    }

  private:
    struct Node {
        Vec3 center{};
        float half = 0.0f;
        Vec3 com{};
        float mass = 0.0f;
        bool leaf = true;
        std::vector<int> points;
        std::array<int, 8> child{};

        Node() {
            child.fill(-1);
        }
    };

    const std::vector<Vec3>& positions_;
    const std::vector<int>& active_nodes_;
    float k_sq_ = 0.0f;
    float theta_ = 0.0f;
    float softening_ = 0.0f;
    std::vector<Node> nodes_;

    static constexpr int kLeafCapacity = 8;
    static constexpr int kMaxDepth = 32;
    static constexpr float kMinHalf = 1e-5f;

    void BuildTree() {
        if (active_nodes_.empty()) {
            return;
        }

        Vec3 min_p = positions_[static_cast<std::size_t>(active_nodes_[0])];
        Vec3 max_p = min_p;
        for (int idx : active_nodes_) {
            const Vec3 p = positions_[static_cast<std::size_t>(idx)];
            min_p.x = std::min(min_p.x, p.x);
            min_p.y = std::min(min_p.y, p.y);
            min_p.z = std::min(min_p.z, p.z);
            max_p.x = std::max(max_p.x, p.x);
            max_p.y = std::max(max_p.y, p.y);
            max_p.z = std::max(max_p.z, p.z);
        }

        const Vec3 span = max_p - min_p;
        const float max_span = std::max({span.x, span.y, span.z});
        const Vec3 center = (min_p + max_p) * 0.5f;

        nodes_.clear();
        nodes_.reserve(active_nodes_.size() * 2);
        Node root;
        root.center = center;
        root.half = std::max(max_span * 0.5f + 1e-3f, 1e-3f);
        nodes_.push_back(std::move(root));

        for (int idx : active_nodes_) {
            InsertPoint(0, idx, 0);
        }
        ComputeMass(0);
    }

    int ChildIndex(const Vec3& center, const Vec3& p) const {
        int oct = 0;
        if (p.x >= center.x) {
            oct |= 1;
        }
        if (p.y >= center.y) {
            oct |= 2;
        }
        if (p.z >= center.z) {
            oct |= 4;
        }
        return oct;
    }

    int EnsureChild(int node_index, int octant) {
        Node& parent = nodes_[static_cast<std::size_t>(node_index)];
        int& child_ref = parent.child[static_cast<std::size_t>(octant)];
        if (child_ref >= 0) {
            return child_ref;
        }

        const float q = parent.half * 0.5f;
        Vec3 offset{(octant & 1) ? q : -q, (octant & 2) ? q : -q, (octant & 4) ? q : -q};

        Node child;
        child.center = parent.center + offset;
        child.half = q;
        child_ref = static_cast<int>(nodes_.size());
        nodes_.push_back(std::move(child));
        return child_ref;
    }

    void Subdivide(int node_index) {
        Node& node = nodes_[static_cast<std::size_t>(node_index)];
        node.leaf = false;
        std::vector<int> old_points = std::move(node.points);
        node.points.clear();
        for (int idx : old_points) {
            const int oct = ChildIndex(node.center, positions_[static_cast<std::size_t>(idx)]);
            const int child = EnsureChild(node_index, oct);
            nodes_[static_cast<std::size_t>(child)].points.push_back(idx);
        }
    }

    void InsertPoint(int node_index, int point_index, int depth) {
        Node& node = nodes_[static_cast<std::size_t>(node_index)];
        if (node.leaf) {
            if (static_cast<int>(node.points.size()) < kLeafCapacity || depth >= kMaxDepth || node.half <= kMinHalf) {
                node.points.push_back(point_index);
                return;
            }
            Subdivide(node_index);
        }

        const int oct = ChildIndex(node.center, positions_[static_cast<std::size_t>(point_index)]);
        const int child = EnsureChild(node_index, oct);
        InsertPoint(child, point_index, depth + 1);
    }

    void ComputeMass(int node_index) {
        Node& node = nodes_[static_cast<std::size_t>(node_index)];
        node.mass = 0.0f;
        node.com = Vec3{};

        if (node.leaf) {
            for (int idx : node.points) {
                const Vec3 p = positions_[static_cast<std::size_t>(idx)];
                node.com += p;
                node.mass += 1.0f;
            }
            if (node.mass > 0.0f) {
                node.com = node.com / node.mass;
            }
            return;
        }

        for (int child_idx : node.child) {
            if (child_idx < 0) {
                continue;
            }
            ComputeMass(child_idx);
            const Node& child = nodes_[static_cast<std::size_t>(child_idx)];
            if (child.mass <= 0.0f) {
                continue;
            }
            node.com += child.com * child.mass;
            node.mass += child.mass;
        }
        if (node.mass > 0.0f) {
            node.com = node.com / node.mass;
        }
    }

    bool ContainsPoint(const Node& node, const Vec3& p) const {
        return (p.x >= node.center.x - node.half && p.x <= node.center.x + node.half &&
                p.y >= node.center.y - node.half && p.y <= node.center.y + node.half &&
                p.z >= node.center.z - node.half && p.z <= node.center.z + node.half);
    }

    void Accumulate(int node_index, int target_index, Vec3& out_force) const {
        const Node& node = nodes_[static_cast<std::size_t>(node_index)];
        if (node.mass <= 0.0f) {
            return;
        }

        const Vec3 target = positions_[static_cast<std::size_t>(target_index)];
        if (node.leaf) {
            for (int idx : node.points) {
                if (idx == target_index) {
                    continue;
                }
                const Vec3 delta = target - positions_[static_cast<std::size_t>(idx)];
                const float d2 = LengthSquared(delta) + softening_;
                out_force += delta * (k_sq_ / d2);
            }
            return;
        }

        const Vec3 delta = target - node.com;
        const float d2 = LengthSquared(delta) + softening_;
        const float dist = std::sqrt(d2);
        const float size = node.half * 2.0f;

        if (!ContainsPoint(node, target) && (size / dist) < theta_) {
            out_force += delta * ((k_sq_ * node.mass) / d2);
            return;
        }

        for (int child_idx : node.child) {
            if (child_idx < 0) {
                continue;
            }
            Accumulate(child_idx, target_index, out_force);
        }
    }
};

BfsGrowthSimulation3D::BfsGrowthSimulation3D(const PuzzleGraphBuilder::GraphData& graph,
                                             int root_node,
                                             Parameters params)
    : graph_(graph),
      params_(params),
      pos_(graph.adjacency.size(), Vec3{}),
      vel_(graph.adjacency.size(), Vec3{}),
      force_(graph.adjacency.size(), Vec3{}),
      dense_index_(graph.adjacency.size(), -1),
      active_(graph.adjacency.size(), 0U),
      discovered_(graph.adjacency.size(), 0U),
      rng_(1337U) {
    if (root_node < 0 || static_cast<std::size_t>(root_node) >= graph_.adjacency.size()) {
        throw std::runtime_error("Invalid root node");
    }
    active_nodes_.reserve(graph.adjacency.size());
    bfs_queue_.resize(0);
    ActivateRoot(root_node);
    active_edges_.reserve(graph.undirected_edges.size());
    edge_indices_.reserve(graph.undirected_edges.size() * 2U);
    active_edge_set_.reserve(graph.undirected_edges.size() * 2U);
}

std::size_t BfsGrowthSimulation3D::TotalNodeCount() const { return graph_.adjacency.size(); }
std::size_t BfsGrowthSimulation3D::ActiveNodeCount() const { return active_nodes_.size(); }
std::size_t BfsGrowthSimulation3D::ActiveEdgeCount() const { return active_edges_.size(); }
bool BfsGrowthSimulation3D::GrowthFinished() const { return bfs_queue_.empty(); }
const std::vector<unsigned int>& BfsGrowthSimulation3D::EdgeIndices() const { return edge_indices_; }

float BfsGrowthSimulation3D::MaxVelocityMagnitude() const {
    float max_v = 0.0f;
    for (int idx : active_nodes_) {
        max_v = std::max(max_v, Length(vel_[static_cast<std::size_t>(idx)]));
    }
    return max_v;
}

bool BfsGrowthSimulation3D::ConsumeEdgeIndexBufferDirty() {
    const bool dirty = edge_index_buffer_dirty_;
    edge_index_buffer_dirty_ = false;
    return dirty;
}

bool BfsGrowthSimulation3D::AddNextBfsNode() {
    if (bfs_queue_.empty()) {
        return false;
    }

    const int u = bfs_queue_.front();
    bfs_queue_.pop_front();

    for (int v : graph_.adjacency[static_cast<std::size_t>(u)]) {
        if (!discovered_[static_cast<std::size_t>(v)]) {
            discovered_[static_cast<std::size_t>(v)] = 1U;
            bfs_queue_.push_back(v);
            active_[static_cast<std::size_t>(v)] = 1U;
            active_nodes_.push_back(v);
            dense_index_[static_cast<std::size_t>(v)] = static_cast<int>(active_nodes_.size() - 1);
            pos_[static_cast<std::size_t>(v)] = pos_[static_cast<std::size_t>(u)] + RandomUnitVector() * params_.k;
            vel_[static_cast<std::size_t>(v)] = Vec3{};
        }
        if (active_[static_cast<std::size_t>(u)] && active_[static_cast<std::size_t>(v)]) {
            const std::uint64_t e_key = EdgeKey(u, v);
            if (active_edge_set_.insert(e_key).second) {
                active_edges_.emplace_back(std::min(u, v), std::max(u, v));
                edge_indices_.push_back(static_cast<unsigned int>(dense_index_[static_cast<std::size_t>(u)]));
                edge_indices_.push_back(static_cast<unsigned int>(dense_index_[static_cast<std::size_t>(v)]));
                edge_index_buffer_dirty_ = true;
            }
        }
    }
    return true;
}

void BfsGrowthSimulation3D::StepPhysics(int iterations) {
    if (active_nodes_.empty()) {
        return;
    }

    for (int iter = 0; iter < iterations; ++iter) {
        for (int idx : active_nodes_) {
            force_[static_cast<std::size_t>(idx)] = Vec3{};
        }

        if (active_nodes_.size() > 1) {
            BarnesHutSolver bh(pos_, active_nodes_, params_.k * params_.k, params_.repulsion_theta, params_.repulsion_softening);
            const std::size_t active_count = active_nodes_.size();
            unsigned int worker_count = static_cast<unsigned int>(params_.worker_threads > 0 ? params_.worker_threads : 1);
            if (active_count < 8192U) {
                worker_count = 1U;
            } else {
                worker_count = std::min<unsigned int>(worker_count, static_cast<unsigned int>(active_count));
            }

            if (worker_count <= 1U) {
                for (int idx : active_nodes_) {
                    force_[static_cast<std::size_t>(idx)] += bh.ComputeRepulsion(idx);
                }
            } else {
                std::vector<std::thread> workers;
                workers.reserve(worker_count);
                for (unsigned int worker = 0; worker < worker_count; ++worker) {
                    const std::size_t begin = (active_count * worker) / worker_count;
                    const std::size_t end = (active_count * (worker + 1U)) / worker_count;
                    workers.emplace_back([&, begin, end]() {
                        for (std::size_t i = begin; i < end; ++i) {
                            const int idx = active_nodes_[i];
                            force_[static_cast<std::size_t>(idx)] += bh.ComputeRepulsion(idx);
                        }
                    });
                }
                for (auto& worker : workers) {
                    worker.join();
                }
            }
        }

        for (const auto& edge : active_edges_) {
            const int u = edge.first;
            const int v = edge.second;
            const Vec3 diff = pos_[static_cast<std::size_t>(u)] - pos_[static_cast<std::size_t>(v)];
            const float dist = Length(diff) + 1e-6f;
            const Vec3 f = diff * (-(dist / params_.k));
            force_[static_cast<std::size_t>(u)] += f;
            force_[static_cast<std::size_t>(v)] -= f;
        }

        for (int idx : active_nodes_) {
            const std::size_t n = static_cast<std::size_t>(idx);
            const Vec3 grav = pos_[n] * (-params_.gravity);
            const Vec3 total = force_[n] + grav;
            vel_[n] = (vel_[n] + total * (params_.dt * params_.intensity)) * params_.damping;
            const float speed = Length(vel_[n]);
            if (speed > params_.max_velocity) {
                vel_[n] *= params_.max_velocity / speed;
            }
            pos_[n] += vel_[n] * params_.dt;
        }
    }
}

void BfsGrowthSimulation3D::BuildPointBuffer(std::vector<float>& point_vertices, float scene_scale) const {
    point_vertices.clear();
    point_vertices.reserve(active_nodes_.size() * 3);
    for (int idx : active_nodes_) {
        const Vec3 p = pos_[static_cast<std::size_t>(idx)] * scene_scale;
        point_vertices.push_back(p.x);
        point_vertices.push_back(p.y);
        point_vertices.push_back(p.z);
    }
}

void BfsGrowthSimulation3D::ActivateRoot(int root) {
    active_[static_cast<std::size_t>(root)] = 1U;
    discovered_[static_cast<std::size_t>(root)] = 1U;
    pos_[static_cast<std::size_t>(root)] = {0.0f, 0.0f, 0.0f};
    vel_[static_cast<std::size_t>(root)] = {0.0f, 0.0f, 0.0f};
    active_nodes_.push_back(root);
    dense_index_[static_cast<std::size_t>(root)] = 0;
    bfs_queue_.push_back(root);
}

Vec3 BfsGrowthSimulation3D::RandomUnitVector() {
    std::normal_distribution<float> normal(0.0f, 1.0f);
    Vec3 v{normal(rng_), normal(rng_), normal(rng_)};
    const float len = Length(v);
    if (len <= 1e-6f) {
        return {1.0f, 0.0f, 0.0f};
    }
    return v / len;
}

}  // namespace graph

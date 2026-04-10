#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

struct Vec2i {
    int x = 0;
    int y = 0;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(const Vec3& v, float s) { return {v.x * s, v.y * s, v.z * s}; }
Vec3 operator/(const Vec3& v, float s) { return {v.x / s, v.y / s, v.z / s}; }
Vec3& operator+=(Vec3& a, const Vec3& b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
Vec3& operator-=(Vec3& a, const Vec3& b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }

float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float LengthSquared(const Vec3& v) { return Dot(v, v); }
float Length(const Vec3& v) { return std::sqrt(LengthSquared(v)); }
Vec3 Normalize(const Vec3& v) {
    const float len = Length(v);
    if (len <= 1e-8f) return {0.0f, 0.0f, 0.0f};
    return v / len;
}

struct Mat4 { std::array<float, 16> v{}; };

Mat4 IdentityMat4() {
    Mat4 out{};
    out.v[0] = out.v[5] = out.v[10] = out.v[15] = 1.0f;
    return out;
}

Mat4 Perspective(float fovy_rad, float aspect, float z_near, float z_far) {
    Mat4 out{};
    const float tan_half = std::tan(0.5f * fovy_rad);
    out.v[0] = 1.0f / (aspect * tan_half);
    out.v[5] = 1.0f / tan_half;
    out.v[10] = -(z_far + z_near) / (z_far - z_near);
    out.v[11] = -1.0f;
    out.v[14] = -(2.0f * z_far * z_near) / (z_far - z_near);
    return out;
}

Mat4 LookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    const Vec3 f = Normalize(center - eye);
    const Vec3 s = Normalize(Cross(f, up));
    const Vec3 u = Cross(s, f);

    Mat4 out = IdentityMat4();
    out.v[0] = s.x; out.v[4] = s.y; out.v[8] = s.z;
    out.v[1] = u.x; out.v[5] = u.y; out.v[9] = u.z;
    out.v[2] = -f.x; out.v[6] = -f.y; out.v[10] = -f.z;
    out.v[12] = -Dot(s, eye);
    out.v[13] = -Dot(u, eye);
    out.v[14] = Dot(f, eye);
    return out;
}

GLuint CompileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) return shader;

    GLint log_length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
    std::string log(static_cast<std::size_t>(log_length), '\0');
    glGetShaderInfoLog(shader, log_length, nullptr, log.data());
    std::cerr << "Shader compile failed: " << log << "\n";
    glDeleteShader(shader);
    return 0;
}

GLuint CreateProgram(const char* vertex_source, const char* fragment_source) {
    const GLuint vs = CompileShader(GL_VERTEX_SHADER, vertex_source);
    if (vs == 0) return 0;
    const GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragment_source);
    if (fs == 0) { glDeleteShader(vs); return 0; }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_TRUE) return program;

    GLint log_length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    std::string log(static_cast<std::size_t>(log_length), '\0');
    glGetProgramInfoLog(program, log_length, nullptr, log.data());
    std::cerr << "Program link failed: " << log << "\n";
    glDeleteProgram(program);
    return 0;
}

std::uint64_t EdgeKey(int a, int b) {
    const std::uint32_t u = static_cast<std::uint32_t>(std::min(a, b));
    const std::uint32_t v = static_cast<std::uint32_t>(std::max(a, b));
    return (static_cast<std::uint64_t>(u) << 32U) | static_cast<std::uint64_t>(v);
}

struct PuzzleConfig {
    int width = 0;
    int height = 0;
    std::vector<Vec2i> piece_sizes;
    std::vector<Vec2i> start_positions;
    bool walled_mode = false;
};

class PuzzleGraphBuilder {
  public:
    explicit PuzzleGraphBuilder(PuzzleConfig config) : config_(std::move(config)) {
        if (config_.piece_sizes.size() != config_.start_positions.size()) {
            throw std::runtime_error("piece_sizes and start_positions size mismatch");
        }
        BuildPieceGroups();
    }

    struct GraphData {
        std::vector<std::vector<int>> adjacency;
        std::vector<std::pair<int, int>> undirected_edges;
    };

    [[nodiscard]] GraphData BuildFullStateGraph(std::size_t max_states = std::numeric_limits<std::size_t>::max(),
                                                bool verbose = true) const {
        std::unordered_map<std::string, int> state_to_index;
        state_to_index.reserve(32768);
        std::vector<std::vector<Vec2i>> states;
        states.reserve(32768);
        std::vector<std::vector<int>> adjacency;
        adjacency.reserve(32768);

        const std::string start_key = EncodeState(config_.start_positions);
        state_to_index.emplace(start_key, 0);
        states.push_back(config_.start_positions);
        adjacency.emplace_back();

        std::deque<int> queue;
        queue.push_back(0);
        std::unordered_set<std::uint64_t> edge_set;
        edge_set.reserve(131072);
        std::vector<std::pair<int, int>> undirected_edges;
        undirected_edges.reserve(131072);

        while (!queue.empty() && states.size() < max_states) {
            const int u = queue.front();
            queue.pop_front();

            ForEachNeighbor(states[static_cast<std::size_t>(u)], [&](const std::vector<Vec2i>& next_state) {
                const std::string key = EncodeState(next_state);
                auto it = state_to_index.find(key);
                int v = -1;
                if (it == state_to_index.end()) {
                    v = static_cast<int>(states.size());
                    state_to_index.emplace(key, v);
                    states.push_back(next_state);
                    adjacency.emplace_back();
                    queue.push_back(v);
                    if (verbose && (states.size() % 1000 == 0)) {
                        std::cout << "Discovered states: " << states.size() << "\r";
                    }
                } else {
                    v = it->second;
                }

                if (u == v) return;
                const std::uint64_t e_key = EdgeKey(u, v);
                if (!edge_set.insert(e_key).second) return;

                adjacency[static_cast<std::size_t>(u)].push_back(v);
                adjacency[static_cast<std::size_t>(v)].push_back(u);
                undirected_edges.emplace_back(std::min(u, v), std::max(u, v));
            });
        }

        if (verbose) {
            std::cout << "\nState graph build finished. Nodes=" << adjacency.size()
                      << ", Edges=" << undirected_edges.size() << "\n";
        }

        return {std::move(adjacency), std::move(undirected_edges)};
    }

  private:
    PuzzleConfig config_;
    std::vector<std::vector<int>> piece_groups_;

    void BuildPieceGroups() {
        std::unordered_map<std::uint32_t, int> key_to_group;
        piece_groups_.clear();
        piece_groups_.reserve(config_.piece_sizes.size());

        for (std::size_t i = 0; i < config_.piece_sizes.size(); ++i) {
            const auto& s = config_.piece_sizes[i];
            const std::uint32_t key = static_cast<std::uint32_t>((s.x << 16) | s.y);
            const auto it = key_to_group.find(key);
            if (it == key_to_group.end()) {
                const int group_id = static_cast<int>(piece_groups_.size());
                key_to_group.emplace(key, group_id);
                piece_groups_.push_back({static_cast<int>(i)});
            } else {
                piece_groups_[static_cast<std::size_t>(it->second)].push_back(static_cast<int>(i));
            }
        }
    }

    [[nodiscard]] bool BuildOccupancy(const std::vector<Vec2i>& positions,
                                      std::vector<std::uint8_t>& occupancy) const {
        occupancy.assign(static_cast<std::size_t>(config_.width * config_.height), 0U);
        auto idx = [this](int x, int y) { return static_cast<std::size_t>(y * config_.width + x); };

        for (std::size_t i = 0; i < positions.size(); ++i) {
            const Vec2i p = positions[i];
            const Vec2i s = config_.piece_sizes[i];
            if (p.x < 0 || p.y < 0 || p.x + s.x > config_.width || p.y + s.y > config_.height) {
                return false;
            }
            for (int yy = p.y; yy < p.y + s.y; ++yy) {
                for (int xx = p.x; xx < p.x + s.x; ++xx) {
                    auto& cell = occupancy[idx(xx, yy)];
                    if (cell != 0U) return false;
                    cell = 1U;
                }
            }
        }
        return true;
    }

    [[nodiscard]] bool CanMoveDown(const std::vector<std::uint8_t>& occ, const Vec2i& p, const Vec2i& s) const {
        if (p.y <= 0) return false;
        for (int xx = p.x; xx < p.x + s.x; ++xx) {
            if (occ[static_cast<std::size_t>((p.y - 1) * config_.width + xx)] != 0U) return false;
        }
        return true;
    }

    [[nodiscard]] bool CanMoveUp(const std::vector<std::uint8_t>& occ, const Vec2i& p, const Vec2i& s) const {
        if (p.y + s.y >= config_.height) return false;
        for (int xx = p.x; xx < p.x + s.x; ++xx) {
            if (occ[static_cast<std::size_t>((p.y + s.y) * config_.width + xx)] != 0U) return false;
        }
        return true;
    }

    [[nodiscard]] bool CanMoveLeft(const std::vector<std::uint8_t>& occ, const Vec2i& p, const Vec2i& s) const {
        if (p.x <= 0) return false;
        for (int yy = p.y; yy < p.y + s.y; ++yy) {
            if (occ[static_cast<std::size_t>(yy * config_.width + (p.x - 1))] != 0U) return false;
        }
        return true;
    }

    [[nodiscard]] bool CanMoveRight(const std::vector<std::uint8_t>& occ, const Vec2i& p, const Vec2i& s) const {
        if (p.x + s.x >= config_.width) return false;
        for (int yy = p.y; yy < p.y + s.y; ++yy) {
            if (occ[static_cast<std::size_t>(yy * config_.width + (p.x + s.x))] != 0U) return false;
        }
        return true;
    }

    template <typename Callback>
    void ForEachNeighbor(const std::vector<Vec2i>& positions, Callback&& callback) const {
        std::vector<std::uint8_t> occupancy;
        if (!BuildOccupancy(positions, occupancy)) return;

        for (std::size_t i = 0; i < positions.size(); ++i) {
            const Vec2i p = positions[i];
            const Vec2i s = config_.piece_sizes[i];
            const bool allow_vertical = (!config_.walled_mode) || (s.x == 1);
            const bool allow_horizontal = (!config_.walled_mode) || (s.y == 1);

            if (allow_vertical && CanMoveDown(occupancy, p, s)) {
                std::vector<Vec2i> next = positions;
                next[i].y -= 1;
                callback(next);
            }
            if (allow_vertical && CanMoveUp(occupancy, p, s)) {
                std::vector<Vec2i> next = positions;
                next[i].y += 1;
                callback(next);
            }
            if (allow_horizontal && CanMoveLeft(occupancy, p, s)) {
                std::vector<Vec2i> next = positions;
                next[i].x -= 1;
                callback(next);
            }
            if (allow_horizontal && CanMoveRight(occupancy, p, s)) {
                std::vector<Vec2i> next = positions;
                next[i].x += 1;
                callback(next);
            }
        }
    }

    [[nodiscard]] std::string EncodeState(const std::vector<Vec2i>& positions) const {
        std::string key;
        key.reserve(positions.size() * 2 + piece_groups_.size());
        for (const auto& group : piece_groups_) {
            std::vector<std::uint16_t> compact;
            compact.reserve(group.size());
            for (int idx : group) {
                const Vec2i p = positions[static_cast<std::size_t>(idx)];
                compact.push_back(static_cast<std::uint16_t>(p.y * config_.width + p.x));
            }
            std::sort(compact.begin(), compact.end());
            for (std::uint16_t code : compact) {
                key.push_back(static_cast<char>(code & 0xFFU));
                key.push_back(static_cast<char>((code >> 8U) & 0xFFU));
            }
            key.push_back(static_cast<char>(0xFFU));
        }
        return key;
    }
};

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
        ActivateRoot(root_node);
        active_edges_.reserve(graph.undirected_edges.size());
        edge_indices_.reserve(graph.undirected_edges.size() * 2U);
    }

    [[nodiscard]] std::size_t TotalNodeCount() const { return graph_.adjacency.size(); }
    [[nodiscard]] std::size_t ActiveNodeCount() const { return active_nodes_.size(); }
    [[nodiscard]] std::size_t ActiveEdgeCount() const { return active_edges_.size(); }
    [[nodiscard]] bool GrowthFinished() const { return bfs_queue_.empty(); }
    [[nodiscard]] const std::vector<unsigned int>& EdgeIndices() const { return edge_indices_; }

    [[nodiscard]] float MaxVelocityMagnitude() const {
        float max_v = 0.0f;
        for (int idx : active_nodes_) {
            max_v = std::max(max_v, Length(vel_[static_cast<std::size_t>(idx)]));
        }
        return max_v;
    }

    bool ConsumeEdgeIndexBufferDirty() {
        const bool dirty = edge_index_buffer_dirty_;
        edge_index_buffer_dirty_ = false;
        return dirty;
    }

    bool AddNextBfsNode() {
        if (bfs_queue_.empty()) return false;

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

    void StepPhysics(int iterations) {
        if (active_nodes_.empty()) return;

        for (int iter = 0; iter < iterations; ++iter) {
            for (int idx : active_nodes_) {
                force_[static_cast<std::size_t>(idx)] = Vec3{};
            }

            if (active_nodes_.size() > 1) {
                BarnesHutSolver bh(pos_, active_nodes_, params_.k * params_.k, params_.repulsion_theta, params_.repulsion_softening);
                const std::size_t active_count = active_nodes_.size();
                unsigned int worker_count = static_cast<unsigned int>(params_.worker_threads > 0 ? params_.worker_threads : 0);
                if (worker_count == 0U) {
                    worker_count = std::max(1U, std::thread::hardware_concurrency());
                }
                if (active_count < 2048U) {
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

            for (const auto& e : active_edges_) {
                const int u = e.first;
                const int v = e.second;
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
                    vel_[n] = vel_[n] * (params_.max_velocity / speed);
                }
                pos_[n] += vel_[n] * params_.dt;
            }
        }
    }

    void BuildPointBuffer(std::vector<float>& point_vertices) const {
        point_vertices.clear();
        point_vertices.reserve(active_nodes_.size() * 3);
        for (int idx : active_nodes_) {
            const Vec3 p = pos_[static_cast<std::size_t>(idx)];
            point_vertices.push_back(p.x);
            point_vertices.push_back(p.y);
            point_vertices.push_back(p.z);
        }
    }

  private:
    class BarnesHutSolver {
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
            if (nodes_.empty()) return force;
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
            Node() { child.fill(-1); }
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
            if (active_nodes_.empty()) return;

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
            if (p.x >= center.x) oct |= 1;
            if (p.y >= center.y) oct |= 2;
            if (p.z >= center.z) oct |= 4;
            return oct;
        }

        int EnsureChild(int node_index, int octant) {
            Node& parent = nodes_[static_cast<std::size_t>(node_index)];
            int& child_ref = parent.child[static_cast<std::size_t>(octant)];
            if (child_ref >= 0) return child_ref;

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
                if (node.mass > 0.0f) node.com = node.com / node.mass;
                return;
            }

            for (int child_idx : node.child) {
                if (child_idx < 0) continue;
                ComputeMass(child_idx);
                const Node& c = nodes_[static_cast<std::size_t>(child_idx)];
                if (c.mass <= 0.0f) continue;
                node.com += c.com * c.mass;
                node.mass += c.mass;
            }
            if (node.mass > 0.0f) node.com = node.com / node.mass;
        }

        bool ContainsPoint(const Node& node, const Vec3& p) const {
            return (p.x >= node.center.x - node.half && p.x <= node.center.x + node.half &&
                    p.y >= node.center.y - node.half && p.y <= node.center.y + node.half &&
                    p.z >= node.center.z - node.half && p.z <= node.center.z + node.half);
        }

        void Accumulate(int node_index, int target_index, Vec3& out_force) const {
            const Node& node = nodes_[static_cast<std::size_t>(node_index)];
            if (node.mass <= 0.0f) return;

            const Vec3 target = positions_[static_cast<std::size_t>(target_index)];

            if (node.leaf) {
                for (int idx : node.points) {
                    if (idx == target_index) continue;
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
                if (child_idx < 0) continue;
                Accumulate(child_idx, target_index, out_force);
            }
        }
    };

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

    void ActivateRoot(int root) {
        active_[static_cast<std::size_t>(root)] = 1U;
        discovered_[static_cast<std::size_t>(root)] = 1U;
        pos_[static_cast<std::size_t>(root)] = {0.0f, 0.0f, 0.0f};
        vel_[static_cast<std::size_t>(root)] = {0.0f, 0.0f, 0.0f};
        active_nodes_.push_back(root);
        dense_index_[static_cast<std::size_t>(root)] = 0;
        bfs_queue_.push_back(root);
    }

    Vec3 RandomUnitVector() {
        std::normal_distribution<float> normal(0.0f, 1.0f);
        Vec3 v{normal(rng_), normal(rng_), normal(rng_)};
        const float len = Length(v);
        if (len <= 1e-6f) return {1.0f, 0.0f, 0.0f};
        return v / len;
    }
};

class GraphRenderer3D {
  public:
    GraphRenderer3D(int width, int height, const char* title)
        : width_(width), height_(height), title_(title) {}

    ~GraphRenderer3D() { Shutdown(); }

    bool Initialize() {
        if (!glfwInit()) {
            std::cerr << "Failed to initialize GLFW\n";
            return false;
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        window_ = glfwCreateWindow(width_, height_, title_, nullptr, nullptr);
        if (!window_) {
            std::cerr << "Failed to create GLFW window\n";
            glfwTerminate();
            return false;
        }

        glfwMakeContextCurrent(window_);
        glfwSwapInterval(1);

        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
            std::cerr << "Failed to initialize GLAD\n";
            return false;
        }

        glfwSetWindowUserPointer(window_, this);
        glfwSetFramebufferSizeCallback(window_, FramebufferSizeCallback);
        glfwSetMouseButtonCallback(window_, MouseButtonCallback);
        glfwSetCursorPosCallback(window_, CursorPosCallback);
        glfwSetScrollCallback(window_, ScrollCallback);

        const char* vertex_source = R"(
            #version 330 core
            layout (location = 0) in vec3 aPos;
            uniform mat4 uProj;
            uniform mat4 uView;
            uniform float uPointSize;
            void main() {
                gl_Position = uProj * uView * vec4(aPos, 1.0);
                gl_PointSize = uPointSize;
            }
        )";

        const char* fragment_source = R"(
            #version 330 core
            out vec4 FragColor;
            uniform vec3 uColor;
            void main() {
                FragColor = vec4(uColor, 1.0);
            }
        )";

        program_ = CreateProgram(vertex_source, fragment_source);
        if (program_ == 0) return false;

        glGenVertexArrays(1, &point_vao_);
        glGenBuffers(1, &point_vbo_);
        glBindVertexArray(point_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, point_vbo_);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, nullptr);
        glEnableVertexAttribArray(0);

        glGenVertexArrays(1, &edge_vao_);
        glGenBuffers(1, &edge_ebo_);
        glBindVertexArray(edge_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, point_vbo_);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, nullptr);
        glEnableVertexAttribArray(0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, edge_ebo_);
        glBindVertexArray(0);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glPointSize(point_size_);
        glLineWidth(line_width_);
        glViewport(0, 0, width_, height_);
        last_time_ = glfwGetTime();
        return true;
    }

    [[nodiscard]] bool ShouldClose() const {
        return window_ == nullptr || glfwWindowShouldClose(window_);
    }

    void SetPointSize(float point_size) {
        point_size_ = point_size;
        if (window_ != nullptr) {
            glPointSize(point_size_);
        }
    }

    void PollEvents() {
        glfwPollEvents();
        if (window_ && glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
    }

    void UpdatePointGeometry(const std::vector<float>& point_vertices) {
        point_count_ = static_cast<GLsizei>(point_vertices.size() / 3U);
        last_points_ = point_vertices;

        glBindBuffer(GL_ARRAY_BUFFER, point_vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(point_vertices.size() * sizeof(float)),
                     point_vertices.data(),
                     GL_STREAM_DRAW);
    }

    void UpdateEdgeIndexBuffer(const std::vector<unsigned int>& edge_indices) {
        edge_index_count_ = static_cast<GLsizei>(edge_indices.size());
        glBindVertexArray(edge_vao_);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, edge_ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(edge_indices.size() * sizeof(unsigned int)),
                     edge_indices.data(),
                     GL_DYNAMIC_DRAW);
        glBindVertexArray(0);
    }

    void RenderFrame(std::size_t active_nodes, std::size_t total_nodes) {
        const double now = glfwGetTime();
        const float dt = static_cast<float>(now - last_time_);
        last_time_ = now;

        if (!dragging_) {
            yaw_ += 12.0f * dt;
        }
        UpdateAutoCamera();

        glClearColor(0.045f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const Vec3 eye{
            target_.x + distance_ * std::cos(yaw_ * kDegToRad) * std::cos(pitch_ * kDegToRad),
            target_.y + distance_ * std::sin(pitch_ * kDegToRad),
            target_.z + distance_ * std::sin(yaw_ * kDegToRad) * std::cos(pitch_ * kDegToRad),
        };
        const Mat4 view = LookAt(eye, target_, {0.0f, 1.0f, 0.0f});
        const Mat4 proj = Perspective(45.0f * kDegToRad, static_cast<float>(width_) / static_cast<float>(height_), 0.1f, 5000.0f);

        glUseProgram(program_);
        glUniformMatrix4fv(glGetUniformLocation(program_, "uView"), 1, GL_FALSE, view.v.data());
        glUniformMatrix4fv(glGetUniformLocation(program_, "uProj"), 1, GL_FALSE, proj.v.data());

        if (edge_index_count_ > 0) {
            glUniform3f(glGetUniformLocation(program_, "uColor"), 0.72f, 0.72f, 0.74f);
            glUniform1f(glGetUniformLocation(program_, "uPointSize"), point_size_);
            glBindVertexArray(edge_vao_);
            glDrawElements(GL_LINES, edge_index_count_, GL_UNSIGNED_INT, nullptr);
        }

        if (point_count_ > 0) {
            glUniform3f(glGetUniformLocation(program_, "uColor"), 0.20f, 0.82f, 1.00f);
            glUniform1f(glGetUniformLocation(program_, "uPointSize"), point_size_);
            glBindVertexArray(point_vao_);
            glDrawArrays(GL_POINTS, 0, point_count_);
        }
        glBindVertexArray(0);
        glfwSwapBuffers(window_);

        if ((frame_counter_ % 60) == 0) {
            std::ostringstream oss;
            oss << title_ << " | Nodes " << active_nodes << "/" << total_nodes
                << " | Edges " << (edge_index_count_ / 2);
            glfwSetWindowTitle(window_, oss.str().c_str());
        }
        ++frame_counter_;
    }

  private:
    GLFWwindow* window_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    const char* title_ = nullptr;

    GLuint program_ = 0;
    GLuint point_vao_ = 0;
    GLuint point_vbo_ = 0;
    GLuint edge_vao_ = 0;
    GLuint edge_ebo_ = 0;
    GLsizei point_count_ = 0;
    GLsizei edge_index_count_ = 0;
    std::vector<float> last_points_;

    float line_width_ = 1.0f;
    float point_size_ = 2.8f;
    Vec3 target_{0.0f, 0.0f, 0.0f};
    float distance_ = 30.0f;
    float yaw_ = -90.0f;
    float pitch_ = 10.0f;
    bool dragging_ = false;
    double last_cursor_x_ = 0.0;
    double last_cursor_y_ = 0.0;
    double last_time_ = 0.0;
    std::uint64_t frame_counter_ = 0;

    void Shutdown() {
        if (point_vbo_ != 0) { glDeleteBuffers(1, &point_vbo_); point_vbo_ = 0; }
        if (point_vao_ != 0) { glDeleteVertexArrays(1, &point_vao_); point_vao_ = 0; }
        if (edge_ebo_ != 0) { glDeleteBuffers(1, &edge_ebo_); edge_ebo_ = 0; }
        if (edge_vao_ != 0) { glDeleteVertexArrays(1, &edge_vao_); edge_vao_ = 0; }
        if (program_ != 0) { glDeleteProgram(program_); program_ = 0; }
        if (window_ != nullptr) { glfwDestroyWindow(window_); window_ = nullptr; }
        glfwTerminate();
    }

    void UpdateAutoCamera() {
        if (last_points_.empty()) return;
        const std::size_t n = last_points_.size() / 3U;

        Vec3 centroid{};
        for (std::size_t i = 0; i < n; ++i) {
            centroid.x += last_points_[i * 3 + 0];
            centroid.y += last_points_[i * 3 + 1];
            centroid.z += last_points_[i * 3 + 2];
        }
        centroid = centroid / static_cast<float>(n);

        float max_dist = 0.0f;
        for (std::size_t i = 0; i < n; ++i) {
            Vec3 p{last_points_[i * 3 + 0], last_points_[i * 3 + 1], last_points_[i * 3 + 2]};
            max_dist = std::max(max_dist, Length(p - centroid));
        }

        const float ideal_distance = std::max(8.0f, max_dist * 2.4f + std::log(static_cast<float>(n) + 1.0f) * 0.8f);
        distance_ = std::clamp(distance_ * 0.92f + ideal_distance * 0.08f, 0.4f, 10000.0f);
        target_ = target_ * 0.92f + centroid * 0.08f;
    }

    void OnFramebufferSize(int width, int height) {
        width_ = std::max(1, width);
        height_ = std::max(1, height);
        glViewport(0, 0, width_, height_);
    }

    void OnMouseButton(int button, int action) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            dragging_ = (action == GLFW_PRESS);
            if (dragging_) {
                glfwGetCursorPos(window_, &last_cursor_x_, &last_cursor_y_);
            }
        }
    }

    void OnCursorPos(double x, double y) {
        if (!dragging_) return;
        const double dx = x - last_cursor_x_;
        const double dy = y - last_cursor_y_;
        last_cursor_x_ = x;
        last_cursor_y_ = y;

        yaw_ += static_cast<float>(dx * 0.28);
        pitch_ -= static_cast<float>(dy * 0.28);
        pitch_ = std::clamp(pitch_, -89.0f, 89.0f);
    }

    void OnScroll(double yoffset) {
        distance_ -= static_cast<float>(yoffset);
        distance_ = std::max(distance_, 0.1f);
    }

    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
        auto* self = reinterpret_cast<GraphRenderer3D*>(glfwGetWindowUserPointer(window));
        if (self) self->OnFramebufferSize(width, height);
    }

    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int /*mods*/) {
        auto* self = reinterpret_cast<GraphRenderer3D*>(glfwGetWindowUserPointer(window));
        if (self) self->OnMouseButton(button, action);
    }

    static void CursorPosCallback(GLFWwindow* window, double x, double y) {
        auto* self = reinterpret_cast<GraphRenderer3D*>(glfwGetWindowUserPointer(window));
        if (self) self->OnCursorPos(x, y);
    }

    static void ScrollCallback(GLFWwindow* window, double /*xoffset*/, double yoffset) {
        auto* self = reinterpret_cast<GraphRenderer3D*>(glfwGetWindowUserPointer(window));
        if (self) self->OnScroll(yoffset);
    }
};

PuzzleConfig MakePuzzle(int width,
                        int height,
                        bool walled_mode,
                        std::initializer_list<Vec2i> piece_sizes,
                        std::initializer_list<Vec2i> start_positions) {
    PuzzleConfig cfg;
    cfg.width = width;
    cfg.height = height;
    cfg.walled_mode = walled_mode;
    cfg.piece_sizes.assign(piece_sizes.begin(), piece_sizes.end());
    cfg.start_positions.assign(start_positions.begin(), start_positions.end());
    return cfg;
}

PuzzleConfig MakePuzzlePreset(int preset_id) {
    switch (preset_id) {
        case 1:
            return MakePuzzle(4, 5, false,
                              {{1, 2}, {1, 2}, {1, 2}, {1, 2}, {2, 2}, {2, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}},
                              {{0, 0}, {0, 2}, {3, 0}, {3, 2}, {1, 0}, {1, 2}, {0, 4}, {3, 4}, {1, 3}, {2, 3}});
        case 2:
            return MakePuzzle(6, 6, true,
                              {{1, 2}, {3, 1}, {1, 2}, {2, 1}, {1, 2}, {2, 1}, {2, 1}, {1, 2}, {1, 2}, {1, 2}, {1, 2}, {2, 1}, {3, 1}},
                              {{1, 0}, {2, 0}, {5, 0}, {0, 2}, {2, 1}, {0, 3}, {2, 3}, {4, 2}, {5, 2}, {0, 4}, {2, 4}, {4, 4}, {3, 5}});
        case 3:
            return MakePuzzle(4, 4, false,
                              {{1, 2}, {2, 1}, {2, 2}},
                              {{2, 0}, {0, 2}, {0, 0}});
        case 4:
            return MakePuzzle(4, 5, false,
                              {{1, 2}, {1, 2}, {1, 2}, {1, 2}, {2, 2}, {2, 1}, {2, 1}},
                              {{0, 0}, {0, 2}, {3, 0}, {3, 2}, {1, 0}, {1, 2}, {1, 3}});
        case 5:
            return MakePuzzle(4, 5, false,
                              {{1, 2}, {1, 2}, {2, 2}, {2, 1}, {2, 1}, {2, 1}},
                              {{0, 0}, {3, 0}, {1, 0}, {1, 2}, {1, 3}, {1, 4}});
        case 6:
            return MakePuzzle(4, 6, false,
                              {{1, 2}, {1, 2}, {1, 2}, {1, 3}, {2, 2}, {2, 1}, {1, 2}, {1, 2}, {1, 1}, {1, 1}, {1, 1}},
                              {{0, 0}, {0, 2}, {3, 0}, {3, 2}, {1, 0}, {1, 2}, {1, 3}, {2, 3}, {0, 4}, {0, 5}, {3, 5}});
        case 7:
            return MakePuzzle(4, 6, false,
                              {{2, 1}, {1, 3}, {1, 3}, {1, 2}, {3, 1}, {1, 4}, {1, 1}},
                              {{1, 0}, {3, 0}, {1, 1}, {3, 3}, {1, 5}, {2, 1}, {1, 4}});
        case 8:
            return MakePuzzle(4, 5, false,
                              {{2, 2}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}},
                              {{1, 0}, {0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {3, 0}, {3, 1}, {3, 2}, {3, 3}, {3, 4}, {1, 2}, {1, 3}, {2, 2}, {2, 3}});
        case 9:
            return MakePuzzle(4, 5, false,
                              {{2, 2}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {2, 1}, {1, 1}, {1, 1}},
                              {{1, 0}, {0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {3, 0}, {3, 1}, {3, 2}, {3, 3}, {3, 4}, {1, 2}, {1, 3}, {2, 3}});
        case 10:
            return MakePuzzle(6, 6, true,
                              {{3, 1}, {1, 3}, {2, 1}, {1, 2}, {2, 1}, {1, 2}, {1, 2}, {1, 2}, {2, 1}, {3, 1}},
                              {{0, 0}, {5, 0}, {3, 2}, {2, 2}, {4, 3}, {3, 3}, {0, 4}, {2, 4}, {4, 4}, {3, 5}});
        case 11:
            return MakePuzzle(6, 6, true,
                              {{1, 3}, {2, 1}, {1, 3}, {2, 1}, {1, 2}, {2, 1}},
                              {{1, 0}, {3, 0}, {5, 0}, {3, 2}, {3, 3}, {4, 4}});
        case 12:
            return MakePuzzle(4, 5, false,
                              {{2, 2}, {2, 1}, {2, 1}, {1, 1}, {1, 1}, {1, 2}, {1, 2}, {2, 1}, {2, 1}},
                              {{0, 0}, {2, 0}, {2, 1}, {0, 2}, {1, 2}, {0, 3}, {1, 3}, {2, 3}, {2, 4}});
        case 13:
            return MakePuzzle(5, 5, false,
                              {{2, 1}, {2, 1}, {2, 2}, {2, 2}, {1, 3}, {2, 1}, {3, 1}},
                              {{0, 0}, {2, 0}, {0, 1}, {2, 1}, {4, 0}, {0, 3}, {2, 3}});
        case 14:
            return MakePuzzle(6, 6, true,
                              {{1, 3}, {2, 1}, {1, 2}, {1, 2}, {1, 2}, {2, 1}, {1, 3}, {3, 1}, {1, 2}, {1, 2}, {2, 1}, {2, 1}, {2, 1}},
                              {{0, 0}, {1, 0}, {1, 1}, {2, 1}, {4, 0}, {3, 2}, {5, 1}, {0, 3}, {3, 3}, {2, 4}, {4, 4}, {0, 5}, {3, 5}});
        case 15:
            return MakePuzzle(6, 6, true,
                              {{2, 1}, {1, 3}, {2, 1}, {1, 3}, {1, 3}, {3, 1}, {1, 2}, {2, 1}},
                              {{0, 0}, {0, 1}, {1, 2}, {3, 1}, {5, 0}, {2, 5}, {0, 4}, {4, 4}});
        case 16:
            return MakePuzzle(6, 6, true,
                              {{3, 1}, {1, 2}, {1, 2}, {2, 1}, {1, 3}, {1, 3}, {2, 1}, {2, 1}, {1, 2}, {1, 2}, {2, 1}, {2, 1}, {2, 1}},
                              {{0, 0}, {3, 0}, {0, 1}, {1, 1}, {4, 1}, {5, 1}, {2, 2}, {0, 3}, {2, 3}, {1, 4}, {4, 4}, {2, 5}, {4, 5}});
        default:
            throw std::runtime_error("Unknown preset id. Valid values: 1..16");
    }
}

struct AppOptions {
    int preset_id = 16;
    int width = 1600;
    int height = 900;
    int physics_iterations_per_frame = 3;
    int nodes_per_frame = 100;
    int worker_threads = 0;
    float theta = 0.78f;
    float damping = 0.95f;
    float intensity = 4.0f;
    float point_size = 2.8f;
};

bool ParseIntArg(const std::string& value, int& out) {
    try {
        out = std::stoi(value);
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseFloatArg(const std::string& value, float& out) {
    try {
        out = std::stof(value);
        return true;
    } catch (...) {
        return false;
    }
}

AppOptions ParseArgs(int argc, char** argv) {
    AppOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto require_value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error(std::string("Missing value for ") + name);
            }
            ++i;
            return argv[i];
        };

        if (arg == "--width") {
            if (!ParseIntArg(require_value("--width"), options.width)) {
                throw std::runtime_error("Invalid --width value");
            }
        } else if (arg == "--preset") {
            if (!ParseIntArg(require_value("--preset"), options.preset_id)) {
                throw std::runtime_error("Invalid --preset value");
            }
        } else if (arg == "--height") {
            if (!ParseIntArg(require_value("--height"), options.height)) {
                throw std::runtime_error("Invalid --height value");
            }
        } else if (arg == "--threads") {
            if (!ParseIntArg(require_value("--threads"), options.worker_threads)) {
                throw std::runtime_error("Invalid --threads value");
            }
        } else if (arg == "--physics-iters") {
            if (!ParseIntArg(require_value("--physics-iters"), options.physics_iterations_per_frame)) {
                throw std::runtime_error("Invalid --physics-iters value");
            }
        } else if (arg == "--nodes-per-frame") {
            if (!ParseIntArg(require_value("--nodes-per-frame"), options.nodes_per_frame)) {
                throw std::runtime_error("Invalid --nodes-per-frame value");
            }
        } else if (arg == "--theta") {
            if (!ParseFloatArg(require_value("--theta"), options.theta)) {
                throw std::runtime_error("Invalid --theta value");
            }
        } else if (arg == "--damping") {
            if (!ParseFloatArg(require_value("--damping"), options.damping)) {
                throw std::runtime_error("Invalid --damping value");
            }
        } else if (arg == "--intensity") {
            if (!ParseFloatArg(require_value("--intensity"), options.intensity)) {
                throw std::runtime_error("Invalid --intensity value");
            }
        } else if (arg == "--point-size") {
            if (!ParseFloatArg(require_value("--point-size"), options.point_size)) {
                throw std::runtime_error("Invalid --point-size value");
            }
        } else if (arg == "--help") {
            std::cout
                << "Usage: graph_bfs_visualizer [options]\n"
                << "  --preset <1..16>\n"
                << "  --width <int>\n"
                << "  --height <int>\n"
                << "  --threads <int>\n"
                << "  --physics-iters <int>\n"
                << "  --nodes-per-frame <int>\n"
                << "  --theta <float>\n"
                << "  --damping <float>\n"
                << "  --intensity <float>\n"
                << "  --point-size <float>\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    options.preset_id = std::clamp(options.preset_id, 1, 16);
    options.width = std::max(320, options.width);
    options.height = std::max(240, options.height);
    options.worker_threads = std::max(0, options.worker_threads);
    options.physics_iterations_per_frame = std::max(1, options.physics_iterations_per_frame);
    options.nodes_per_frame = std::max(1, options.nodes_per_frame);
    options.theta = std::clamp(options.theta, 0.2f, 2.0f);
    options.damping = std::clamp(options.damping, 0.1f, 0.999f);
    options.intensity = std::max(0.01f, options.intensity);
    options.point_size = std::clamp(options.point_size, 1.0f, 12.0f);
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const AppOptions options = ParseArgs(argc, argv);
        const PuzzleConfig puzzle = MakePuzzlePreset(options.preset_id);
        const PuzzleGraphBuilder builder(puzzle);
        const auto graph = builder.BuildFullStateGraph();

        BfsGrowthSimulation3D::Parameters params;
        params.k = 0.05f;
        params.gravity = 0.01f;
        params.damping = options.damping;
        params.dt = 0.05f;
        params.intensity = options.intensity;
        params.repulsion_theta = options.theta;
        params.repulsion_softening = 1e-4f;
        params.max_velocity = 10.0f;
        params.worker_threads = options.worker_threads;

        BfsGrowthSimulation3D sim(graph, 0, params);

        GraphRenderer3D renderer(options.width, options.height, "C++ BFS Graph Growth (Barnes-Hut)");
        if (!renderer.Initialize()) return 1;
        renderer.SetPointSize(options.point_size);

        std::vector<float> point_vertices;

        auto last_log = std::chrono::steady_clock::now();
        while (!renderer.ShouldClose()) {
            renderer.PollEvents();

            const int burst = std::max(1, static_cast<int>(sim.ActiveNodeCount() / 30U));
            const int add_budget = std::min(options.nodes_per_frame, burst);
            for (int i = 0; i < add_budget; ++i) {
                if (!sim.AddNextBfsNode()) break;
            }

            if (!sim.GrowthFinished() || sim.MaxVelocityMagnitude() > 0.04f) {
                sim.StepPhysics(options.physics_iterations_per_frame);
            }

            sim.BuildPointBuffer(point_vertices);
            renderer.UpdatePointGeometry(point_vertices);
            if (sim.ConsumeEdgeIndexBufferDirty()) {
                renderer.UpdateEdgeIndexBuffer(sim.EdgeIndices());
            }
            renderer.RenderFrame(sim.ActiveNodeCount(), sim.TotalNodeCount());

            const auto now = std::chrono::steady_clock::now();
            if (now - last_log > std::chrono::seconds(1)) {
                last_log = now;
                std::cout << "Active nodes: " << sim.ActiveNodeCount() << "/" << sim.TotalNodeCount()
                          << ", active edges: " << sim.ActiveEdgeCount()
                          << ", max|v|=" << std::fixed << std::setprecision(4) << sim.MaxVelocityMagnitude() << "\n";
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}

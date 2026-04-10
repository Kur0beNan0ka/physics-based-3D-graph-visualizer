#include "Klotski.hpp"

#include "GraphHelper.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace graph {

namespace {

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

std::string ColorForPiece(std::size_t piece_index) {
    static constexpr std::array<const char*, 16> kPalette = {
        "#FF6B6B", "#FFD166", "#06D6A0", "#4CC9F0",
        "#F72585", "#B8F2E6", "#F4A261", "#90BE6D",
        "#F9C74F", "#577590", "#43AA8B", "#F28482",
        "#84A59D", "#9B5DE5", "#00BBF9", "#FF99C8",
    };
    return kPalette[piece_index % kPalette.size()];
}

}  // namespace

PuzzleGraphBuilder::PuzzleGraphBuilder(PuzzleConfig config) : config_(std::move(config)) {
    if (config_.piece_sizes.size() != config_.start_positions.size()) {
        throw std::runtime_error("piece_sizes and start_positions size mismatch");
    }
    BuildPieceGroups();
}

PuzzleGraphBuilder::GraphData PuzzleGraphBuilder::BuildFullStateGraph(std::size_t max_states, bool verbose) const {
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

            if (u == v) {
                return;
            }

            const std::uint64_t e_key = EdgeKey(u, v);
            if (!edge_set.insert(e_key).second) {
                return;
            }

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

void PuzzleGraphBuilder::BuildPieceGroups() {
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

bool PuzzleGraphBuilder::BuildOccupancy(const std::vector<Vec2i>& positions,
                                        std::vector<std::uint8_t>& occupancy) const {
    occupancy.assign(static_cast<std::size_t>(config_.width * config_.height), 0U);
    auto idx = [this](int x, int y) {
        return static_cast<std::size_t>(y * config_.width + x);
    };

    for (std::size_t i = 0; i < positions.size(); ++i) {
        const Vec2i p = positions[i];
        const Vec2i s = config_.piece_sizes[i];
        if (p.x < 0 || p.y < 0 || p.x + s.x > config_.width || p.y + s.y > config_.height) {
            return false;
        }
        for (int yy = p.y; yy < p.y + s.y; ++yy) {
            for (int xx = p.x; xx < p.x + s.x; ++xx) {
                auto& cell = occupancy[idx(xx, yy)];
                if (cell != 0U) {
                    return false;
                }
                cell = 1U;
            }
        }
    }
    return true;
}

bool PuzzleGraphBuilder::CanMoveDown(const std::vector<std::uint8_t>& occ, const Vec2i& p, const Vec2i& s) const {
    if (p.y <= 0) {
        return false;
    }
    for (int xx = p.x; xx < p.x + s.x; ++xx) {
        if (occ[static_cast<std::size_t>((p.y - 1) * config_.width + xx)] != 0U) {
            return false;
        }
    }
    return true;
}

bool PuzzleGraphBuilder::CanMoveUp(const std::vector<std::uint8_t>& occ, const Vec2i& p, const Vec2i& s) const {
    if (p.y + s.y >= config_.height) {
        return false;
    }
    for (int xx = p.x; xx < p.x + s.x; ++xx) {
        if (occ[static_cast<std::size_t>((p.y + s.y) * config_.width + xx)] != 0U) {
            return false;
        }
    }
    return true;
}

bool PuzzleGraphBuilder::CanMoveLeft(const std::vector<std::uint8_t>& occ, const Vec2i& p, const Vec2i& s) const {
    if (p.x <= 0) {
        return false;
    }
    for (int yy = p.y; yy < p.y + s.y; ++yy) {
        if (occ[static_cast<std::size_t>(yy * config_.width + (p.x - 1))] != 0U) {
            return false;
        }
    }
    return true;
}

bool PuzzleGraphBuilder::CanMoveRight(const std::vector<std::uint8_t>& occ, const Vec2i& p, const Vec2i& s) const {
    if (p.x + s.x >= config_.width) {
        return false;
    }
    for (int yy = p.y; yy < p.y + s.y; ++yy) {
        if (occ[static_cast<std::size_t>(yy * config_.width + (p.x + s.x))] != 0U) {
            return false;
        }
    }
    return true;
}

std::string PuzzleGraphBuilder::EncodeState(const std::vector<Vec2i>& positions) const {
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

template <typename Callback>
void PuzzleGraphBuilder::ForEachNeighbor(const std::vector<Vec2i>& positions, Callback&& callback) const {
    std::vector<std::uint8_t> occupancy;
    if (!BuildOccupancy(positions, occupancy)) {
        return;
    }

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

void WritePuzzleIllustrationSvg(const PuzzleConfig& config,
                                const std::filesystem::path& output_path) {
    if (config.piece_sizes.size() != config.start_positions.size()) {
        throw std::runtime_error("piece_sizes and start_positions size mismatch");
    }

    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    constexpr int kCell = 120;
    constexpr int kMargin = 24;
    constexpr int kInset = 8;
    const int canvas_width = config.width * kCell + kMargin * 2;
    const int canvas_height = config.height * kCell + kMargin * 2;

    std::ofstream svg(output_path, std::ios::binary);
    if (!svg) {
        throw std::runtime_error("Failed to open illustration output path");
    }

    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << canvas_width
        << "\" height=\"" << canvas_height << "\" viewBox=\"0 0 " << canvas_width
        << " " << canvas_height << "\">\n";
    svg << "  <rect width=\"100%\" height=\"100%\" fill=\"#0B0B49\"/>\n";
    svg << "  <rect x=\"" << kMargin / 2 << "\" y=\"" << kMargin / 2
        << "\" width=\"" << canvas_width - kMargin
        << "\" height=\"" << canvas_height - kMargin
        << "\" fill=\"none\" stroke=\"white\" stroke-width=\"8\" rx=\"18\"/>\n";

    if (config.walled_mode) {
        for (int y = 1; y < config.height; ++y) {
            for (int x = 1; x < config.width; ++x) {
                const int cx = kMargin + x * kCell;
                const int cy = kMargin + y * kCell;
                svg << "  <circle cx=\"" << cx << "\" cy=\"" << cy
                    << "\" r=\"5\" fill=\"#7C7F8A\" opacity=\"0.75\"/>\n";
            }
        }
    }

    for (std::size_t i = 0; i < config.piece_sizes.size(); ++i) {
        const Vec2i size = config.piece_sizes[i];
        const Vec2i pos = config.start_positions[i];
        const int x = kMargin + pos.x * kCell + kInset;
        const int y = kMargin + pos.y * kCell + kInset;
        const int width = size.x * kCell - kInset * 2;
        const int height = size.y * kCell - kInset * 2;
        const std::string color = ColorForPiece(i);

        svg << "  <rect x=\"" << x << "\" y=\"" << y
            << "\" width=\"" << width << "\" height=\"" << height
            << "\" rx=\"16\" fill=\"" << color
            << "\" stroke=\"#FFFFFF\" stroke-opacity=\"0.60\" stroke-width=\"3\"/>\n";
    }

    svg << "</svg>\n";
}

}  // namespace graph

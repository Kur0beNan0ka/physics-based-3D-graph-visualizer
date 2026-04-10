#pragma once

#include "GraphCommon.hpp"

#include <cstddef>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace graph {

struct PuzzleConfig {
    int width = 0;
    int height = 0;
    std::vector<Vec2i> piece_sizes;
    std::vector<Vec2i> start_positions;
    bool walled_mode = false;
};

class PuzzleGraphBuilder {
  public:
    struct GraphData {
        std::vector<std::vector<int>> adjacency;
        std::vector<std::pair<int, int>> undirected_edges;
    };

    explicit PuzzleGraphBuilder(PuzzleConfig config);

    [[nodiscard]] GraphData BuildFullStateGraph(std::size_t max_states = std::numeric_limits<std::size_t>::max(),
                                                bool verbose = true) const;

  private:
    PuzzleConfig config_;
    std::vector<std::vector<int>> piece_groups_;

    void BuildPieceGroups();
    [[nodiscard]] bool BuildOccupancy(const std::vector<Vec2i>& positions,
                                      std::vector<std::uint8_t>& occupancy) const;
    [[nodiscard]] bool CanMoveDown(const std::vector<std::uint8_t>& occ, const Vec2i& p, const Vec2i& s) const;
    [[nodiscard]] bool CanMoveUp(const std::vector<std::uint8_t>& occ, const Vec2i& p, const Vec2i& s) const;
    [[nodiscard]] bool CanMoveLeft(const std::vector<std::uint8_t>& occ, const Vec2i& p, const Vec2i& s) const;
    [[nodiscard]] bool CanMoveRight(const std::vector<std::uint8_t>& occ, const Vec2i& p, const Vec2i& s) const;

    template <typename Callback>
    void ForEachNeighbor(const std::vector<Vec2i>& positions, Callback&& callback) const;

    [[nodiscard]] std::string EncodeState(const std::vector<Vec2i>& positions) const;
};

PuzzleConfig MakePuzzlePreset(int preset_id);
void WritePuzzleIllustrationSvg(const PuzzleConfig& config,
                                const std::filesystem::path& output_path);

}  // namespace graph

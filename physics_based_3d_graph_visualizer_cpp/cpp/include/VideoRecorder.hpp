#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace graph {

enum class RenderPhase {
    Growth,
    Viewing,
    Finished,
};

struct RecorderStatus {
    RenderPhase phase = RenderPhase::Finished;
    float progress = 0.0f;
    float elapsed = 0.0f;
    float total_duration = 0.0f;
    std::uint64_t frame_count = 0;
    std::uint64_t total_frames = 0;
    std::size_t current_nodes = 0;
    std::size_t total_nodes = 0;
};

class AutoVideoRecorder {
  public:
    explicit AutoVideoRecorder(int fps = 60);
    ~AutoVideoRecorder();

    void StartAutoRender(const std::string& output_path,
                         int width,
                         int height,
                         std::size_t total_nodes,
                         std::string overlay_label = {},
                         float viewing_duration = 8.0f,
                         int nodes_per_frame = 2);
    RecorderStatus Update(float delta_time, std::size_t current_node_count);
    void CaptureFrame();
    void Stop();

    [[nodiscard]] bool IsRecording() const;
    [[nodiscard]] bool ShouldAddNodes() const;
    [[nodiscard]] RenderPhase Phase() const;
    [[nodiscard]] int NodesPerFrame() const;
    [[nodiscard]] float ViewingDurationRemaining() const;
    [[nodiscard]] std::uint64_t TotalFrames() const;
    [[nodiscard]] const std::filesystem::path& OutputDirectory() const;

  private:
    struct FrameData {
        std::uint64_t index = 0;
        std::vector<std::uint8_t> rgb;
    };

    int fps_ = 60;
    int width_ = 0;
    int height_ = 0;
    std::size_t total_nodes_ = 0;
    std::size_t current_nodes_ = 0;
    int nodes_per_frame_ = 2;
    float viewing_duration_total_ = 8.0f;
    float viewing_duration_remaining_ = 8.0f;
    RenderPhase phase_ = RenderPhase::Finished;
    bool is_recording_ = false;
    bool growth_complete_ = false;
    std::uint64_t frame_count_ = 0;
    std::uint64_t total_frames_ = 0;
    std::filesystem::path requested_output_path_;
    std::filesystem::path output_directory_;
    std::string overlay_label_;
    bool encode_succeeded_ = false;

    mutable std::mutex mutex_;
    std::condition_variable queue_cv_;
    std::deque<FrameData> frame_queue_;
    std::thread writer_thread_;
    bool stop_writer_ = false;

    void SwitchToViewing();
    void FinishRendering();
    RecorderStatus CurrentStatus() const;
    void WriterLoop();
    void WriteManifest() const;
    void EncodeVideoIfPossible();
};

}  // namespace graph

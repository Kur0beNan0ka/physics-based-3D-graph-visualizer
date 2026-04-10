#include "VideoRecorder.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace graph {

namespace {

void AlphaFillRect(std::vector<std::uint8_t>& rgb,
                   int width,
                   int height,
                   int x0,
                   int y0,
                   int x1,
                   int y1,
                   const std::array<std::uint8_t, 3>& color,
                   float alpha) {
    if (width <= 0 || height <= 0 || rgb.empty()) {
        return;
    }
    x0 = std::clamp(x0, 0, width);
    x1 = std::clamp(x1, 0, width);
    y0 = std::clamp(y0, 0, height);
    y1 = std::clamp(y1, 0, height);
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    const float a = std::clamp(alpha, 0.0f, 1.0f);
    const float inv_a = 1.0f - a;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const std::size_t idx = static_cast<std::size_t>((y * width + x) * 3);
            rgb[idx + 0] = static_cast<std::uint8_t>(rgb[idx + 0] * inv_a + color[0] * a);
            rgb[idx + 1] = static_cast<std::uint8_t>(rgb[idx + 1] * inv_a + color[1] * a);
            rgb[idx + 2] = static_cast<std::uint8_t>(rgb[idx + 2] * inv_a + color[2] * a);
        }
    }
}

void AddOverlay(std::vector<std::uint8_t>& rgb,
                int width,
                int height,
                const RecorderStatus& status) {
    if (width <= 0 || height <= 0) {
        return;
    }

    AlphaFillRect(rgb, width, height, 0, 0, width, 50, {0, 0, 0}, 0.35f);

    std::array<std::uint8_t, 3> accent{0, 255, 128};
    if (status.phase == RenderPhase::Viewing) {
        accent = {255, 180, 48};
    } else if (status.phase == RenderPhase::Finished) {
        accent = {0, 220, 255};
    }

    AlphaFillRect(rgb, width, height, 0, 0, width, 4, accent, 1.0f);

    const int bar_margin = 18;
    const int bar_width = std::max(120, width / 3);
    const int bar_height = 10;
    const int bar_x = width - bar_width - bar_margin;
    const int bar_y = 18;

    AlphaFillRect(rgb, width, height, bar_x, bar_y, bar_x + bar_width, bar_y + bar_height, {60, 60, 72}, 0.85f);
    const int fill_width = static_cast<int>(std::round(bar_width * std::clamp(status.progress, 0.0f, 1.0f)));
    AlphaFillRect(rgb, width, height, bar_x, bar_y, bar_x + fill_width, bar_y + bar_height, accent, 0.95f);
}

std::filesystem::path MakeOutputDirectory(const std::filesystem::path& requested_output_path) {
    const std::filesystem::path parent = requested_output_path.has_parent_path()
                                             ? requested_output_path.parent_path()
                                             : std::filesystem::current_path();
    const std::string stem = requested_output_path.stem().string().empty()
                                 ? std::string("render")
                                 : requested_output_path.stem().string();

    std::filesystem::path candidate = parent / (stem + "_frames");
    if (!std::filesystem::exists(candidate)) {
        return candidate;
    }

    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &tt);
#else
    localtime_r(&tt, &local_tm);
#endif
    std::ostringstream suffix;
    suffix << std::put_time(&local_tm, "%Y%m%d_%H%M%S");
    return parent / (stem + "_frames_" + suffix.str());
}

}  // namespace

AutoVideoRecorder::AutoVideoRecorder(int fps) : fps_(std::max(1, fps)) {}

AutoVideoRecorder::~AutoVideoRecorder() {
    Stop();
}

void AutoVideoRecorder::StartAutoRender(const std::string& output_path,
                                        int width,
                                        int height,
                                        std::size_t total_nodes,
                                        float viewing_duration,
                                        int nodes_per_frame) {
    Stop();

    width_ = width;
    height_ = height;
    total_nodes_ = total_nodes;
    current_nodes_ = 0;
    nodes_per_frame_ = std::max(1, nodes_per_frame);
    viewing_duration_total_ = std::max(0.0f, viewing_duration);
    viewing_duration_remaining_ = viewing_duration_total_;
    phase_ = RenderPhase::Growth;
    is_recording_ = true;
    growth_complete_ = false;
    frame_count_ = 0;
    total_frames_ = 0;
    requested_output_path_ = output_path;
    output_directory_ = MakeOutputDirectory(requested_output_path_);
    std::filesystem::create_directories(output_directory_);
    frame_queue_.clear();

    stop_writer_ = false;
    writer_thread_ = std::thread(&AutoVideoRecorder::WriterLoop, this);
    WriteManifest();

    std::cout << "\nStarting offline render capture\n"
              << "  Output directory: " << output_directory_.string() << "\n"
              << "  Resolution: " << width_ << "x" << height_ << " @ " << fps_ << "fps\n"
              << "  Growth phase nodes: " << total_nodes_ << " (" << nodes_per_frame_ << " nodes/frame)\n"
              << "  Viewing phase: " << viewing_duration_total_ << " seconds\n";
}

RecorderStatus AutoVideoRecorder::Update(float delta_time, std::size_t current_node_count) {
    current_nodes_ = current_node_count;
    if (!is_recording_) {
        return CurrentStatus();
    }

    if (phase_ == RenderPhase::Growth) {
        if (current_nodes_ >= total_nodes_ && !growth_complete_) {
            SwitchToViewing();
        }
    } else if (phase_ == RenderPhase::Viewing) {
        viewing_duration_remaining_ = std::max(0.0f, viewing_duration_remaining_ - std::max(0.0f, delta_time));
        if (viewing_duration_remaining_ <= 0.0f) {
            FinishRendering();
        }
    }

    return CurrentStatus();
}

void AutoVideoRecorder::CaptureFrame() {
    if (!is_recording_ || width_ <= 0 || height_ <= 0) {
        return;
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 3U);
    glReadBuffer(GL_FRONT);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    std::vector<std::uint8_t> flipped(pixels.size());
    for (int y = 0; y < height_; ++y) {
        const std::size_t src = static_cast<std::size_t>(y * width_ * 3);
        const std::size_t dst = static_cast<std::size_t>((height_ - 1 - y) * width_ * 3);
        std::copy_n(pixels.data() + src, static_cast<std::size_t>(width_ * 3), flipped.data() + dst);
    }

    AddOverlay(flipped, width_, height_, CurrentStatus());

    std::unique_lock lock(mutex_);
    if (frame_queue_.size() >= 120U) {
        return;
    }
    frame_queue_.push_back(FrameData{total_frames_, std::move(flipped)});
    ++frame_count_;
    ++total_frames_;
    lock.unlock();
    queue_cv_.notify_one();
}

void AutoVideoRecorder::Stop() {
    {
        std::lock_guard lock(mutex_);
        is_recording_ = false;
        stop_writer_ = true;
    }
    queue_cv_.notify_all();
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }
    WriteManifest();
}

bool AutoVideoRecorder::IsRecording() const { return is_recording_; }

bool AutoVideoRecorder::ShouldAddNodes() const {
    return is_recording_ &&
           phase_ == RenderPhase::Growth &&
           current_nodes_ < total_nodes_;
}

RenderPhase AutoVideoRecorder::Phase() const { return phase_; }
int AutoVideoRecorder::NodesPerFrame() const { return nodes_per_frame_; }
float AutoVideoRecorder::ViewingDurationRemaining() const { return viewing_duration_remaining_; }
std::uint64_t AutoVideoRecorder::TotalFrames() const { return total_frames_; }
const std::filesystem::path& AutoVideoRecorder::OutputDirectory() const { return output_directory_; }

void AutoVideoRecorder::SwitchToViewing() {
    phase_ = RenderPhase::Viewing;
    growth_complete_ = true;
    std::cout << "Growth complete. Entering viewing phase for "
              << viewing_duration_total_ << " seconds.\n";
}

void AutoVideoRecorder::FinishRendering() {
    phase_ = RenderPhase::Finished;
    is_recording_ = false;
    std::cout << "Capture finished. Wrote " << total_frames_
              << " frames to " << output_directory_.string() << "\n";
}

RecorderStatus AutoVideoRecorder::CurrentStatus() const {
    RecorderStatus status;
    status.phase = phase_;
    status.frame_count = frame_count_;
    status.total_frames = total_frames_;
    status.current_nodes = current_nodes_;
    status.total_nodes = total_nodes_;

    if (phase_ == RenderPhase::Growth) {
        status.progress = total_nodes_ == 0 ? 1.0f : static_cast<float>(current_nodes_) / static_cast<float>(total_nodes_);
    } else {
        status.progress = 1.0f;
        status.total_duration = viewing_duration_total_;
        status.elapsed = viewing_duration_total_ - viewing_duration_remaining_;
    }
    return status;
}

void AutoVideoRecorder::WriterLoop() {
    for (;;) {
        FrameData frame;
        {
            std::unique_lock lock(mutex_);
            queue_cv_.wait(lock, [&]() {
                return stop_writer_ || !frame_queue_.empty();
            });

            if (frame_queue_.empty()) {
                if (stop_writer_) {
                    break;
                }
                continue;
            }

            frame = std::move(frame_queue_.front());
            frame_queue_.pop_front();
        }

        std::ostringstream filename;
        filename << "frame_" << std::setw(6) << std::setfill('0') << frame.index << ".ppm";
        const std::filesystem::path out_path = output_directory_ / filename.str();

        std::ofstream out(out_path, std::ios::binary);
        if (!out) {
            continue;
        }

        out << "P6\n" << width_ << " " << height_ << "\n255\n";
        out.write(reinterpret_cast<const char*>(frame.rgb.data()),
                  static_cast<std::streamsize>(frame.rgb.size()));
    }
}

void AutoVideoRecorder::WriteManifest() const {
    if (output_directory_.empty()) {
        return;
    }

    std::ofstream out(output_directory_ / "README.txt", std::ios::binary);
    if (!out) {
        return;
    }

    out << "Offline frame capture generated by physics_based_3d_graph_visualizer_cpp\n\n";
    out << "Requested output path: " << requested_output_path_.string() << "\n";
    out << "Stored frames: " << total_frames_ << "\n";
    out << "Resolution: " << width_ << "x" << height_ << "\n";
    out << "FPS: " << fps_ << "\n\n";
    out << "Frames are stored as PPM images so the project stays dependency-free.\n";
    out << "To encode them into H.264 later, install ffmpeg and run:\n\n";
    out << "ffmpeg -framerate " << fps_
        << " -i frame_%06d.ppm -pix_fmt yuv420p output.mp4\n";
}

}  // namespace graph

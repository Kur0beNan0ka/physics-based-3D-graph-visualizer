#include "GraphRender.hpp"
#include "Klotski.hpp"
#include "GraphPhysics.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace graph {

struct AppOptions {
    int preset_id = 16;
    int width = 1920;
    int height = 1080;
    int physics_iterations_per_frame = 3;
    int nodes_per_frame = 100;
    int worker_threads = 0;
    int fps = 60;
    float theta = 0.78f;
    float damping = 0.95f;
    float intensity = 4.0f;
    float point_size = 4.8f;
    float line_width = 1.15f;
    float scene_scale = 2.2f;
    float viewing_duration = 10.0f;
    bool visible = true;
    bool skip_interactive = false;
    std::string video_output;
    std::string figure_output;
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

        if (arg == "--preset") {
            if (!ParseIntArg(require_value("--preset"), options.preset_id)) {
                throw std::runtime_error("Invalid --preset value");
            }
        } else if (arg == "--width") {
            if (!ParseIntArg(require_value("--width"), options.width)) {
                throw std::runtime_error("Invalid --width value");
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
        } else if (arg == "--fps") {
            if (!ParseIntArg(require_value("--fps"), options.fps)) {
                throw std::runtime_error("Invalid --fps value");
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
        } else if (arg == "--line-width") {
            if (!ParseFloatArg(require_value("--line-width"), options.line_width)) {
                throw std::runtime_error("Invalid --line-width value");
            }
        } else if (arg == "--scene-scale") {
            if (!ParseFloatArg(require_value("--scene-scale"), options.scene_scale)) {
                throw std::runtime_error("Invalid --scene-scale value");
            }
        } else if (arg == "--viewing-duration") {
            if (!ParseFloatArg(require_value("--viewing-duration"), options.viewing_duration)) {
                throw std::runtime_error("Invalid --viewing-duration value");
            }
        } else if (arg == "--video-output") {
            options.video_output = require_value("--video-output");
        } else if (arg == "--figure-output") {
            options.figure_output = require_value("--figure-output");
        } else if (arg == "--hidden") {
            options.visible = false;
        } else if (arg == "--no-interactive") {
            options.skip_interactive = true;
        } else if (arg == "--help") {
            std::cout
                << "Usage: graph_bfs_visualizer [options]\n"
                << "  --preset <1..16>\n"
                << "  --width <int>\n"
                << "  --height <int>\n"
                << "  --threads <int>\n"
                << "  --physics-iters <int>\n"
                << "  --nodes-per-frame <int>\n"
                << "  --fps <int>\n"
                << "  --theta <float>\n"
                << "  --damping <float>\n"
                << "  --intensity <float>\n"
                << "  --point-size <float>\n"
                << "  --line-width <float>\n"
                << "  --scene-scale <float>\n"
                << "  --viewing-duration <float>\n"
                << "  --video-output <path>\n"
                << "  --figure-output <path>\n"
                << "  --hidden\n"
                << "  --no-interactive\n";
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
    options.fps = std::max(1, options.fps);
    options.theta = std::clamp(options.theta, 0.2f, 2.0f);
    options.damping = std::clamp(options.damping, 0.1f, 0.999f);
    options.intensity = std::max(0.01f, options.intensity);
    options.point_size = std::clamp(options.point_size, 1.0f, 32.0f);
    options.line_width = std::clamp(options.line_width, 1.0f, 6.0f);
    options.scene_scale = std::max(0.1f, options.scene_scale);
    options.viewing_duration = std::max(0.0f, options.viewing_duration);
    return options;
}

}  // namespace graph

int main(int argc, char** argv) {
    try {
        const graph::AppOptions options = graph::ParseArgs(argc, argv);
        const graph::PuzzleConfig puzzle = graph::MakePuzzlePreset(options.preset_id);
        if (!options.figure_output.empty()) {
            graph::WritePuzzleIllustrationSvg(puzzle, options.figure_output);
        }

        const graph::PuzzleGraphBuilder builder(puzzle);
        const auto graph_data = builder.BuildFullStateGraph();

        graph::BfsGrowthSimulation3D::Parameters sim_params;
        sim_params.k = 0.05f;
        sim_params.gravity = 0.01f;
        sim_params.damping = options.damping;
        sim_params.dt = 0.05f;
        sim_params.intensity = options.intensity;
        sim_params.repulsion_theta = options.theta;
        sim_params.repulsion_softening = 1e-4f;
        sim_params.max_velocity = 10.0f;
        sim_params.worker_threads = options.worker_threads;

        graph::BfsGrowthSimulation3D sim(graph_data, 0, sim_params);

        graph::GraphRendererSettings renderer_settings;
        renderer_settings.width = options.width;
        renderer_settings.height = options.height;
        renderer_settings.visible = options.visible;
        renderer_settings.line_width = options.line_width;
        renderer_settings.point_size = options.point_size;

        graph::GraphRenderer3D renderer(renderer_settings, "C++ BFS Graph Growth");
        if (!renderer.Initialize()) {
            return 1;
        }
        renderer.SetPointSize(options.point_size);
        renderer.SetLineWidth(options.line_width);

        if (!options.video_output.empty()) {
            renderer.RenderVideo(sim,
                                 options.video_output,
                                 options.viewing_duration,
                                 options.nodes_per_frame,
                                 options.physics_iterations_per_frame,
                                 options.fps,
                                 options.scene_scale);
        }

        if (!options.skip_interactive && options.visible && !renderer.ShouldClose()) {
            renderer.RunInteractive(sim,
                                    options.nodes_per_frame,
                                    options.physics_iterations_per_frame,
                                    options.scene_scale);
        }
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}

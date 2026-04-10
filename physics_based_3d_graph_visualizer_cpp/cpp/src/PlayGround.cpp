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
    float orbit_speed = 24.0f;
    float fov_degrees = 40.0f;
    bool visible = true;
    bool skip_interactive = false;
    std::string video_output;
    std::string figure_output;
    bool custom_physics_iterations_per_frame = false;
    bool custom_nodes_per_frame = false;
    bool custom_theta = false;
    bool custom_damping = false;
    bool custom_intensity = false;
    bool custom_point_size = false;
    bool custom_line_width = false;
    bool custom_scene_scale = false;
    bool custom_orbit_speed = false;
    bool custom_fov_degrees = false;
};

struct SimulationProfile {
    std::string preset_name;
    int physics_iterations_per_frame = 1;
    int nodes_per_frame = 24;
    float theta = 0.85f;
    float damping = 0.95f;
    float intensity = 4.0f;
    float point_size = 4.8f;
    float line_width = 1.15f;
    float scene_scale = 2.2f;
    float orbit_speed = 24.0f;
    float fov_degrees = 40.0f;
};

const char* PresetName(int preset_id) {
    switch (preset_id) {
        case 1: return "start_toy";
        case 2: return "start_toy2";
        case 3: return "start_toy3";
        case 4: return "start_toy4";
        case 5: return "start_toy5";
        case 6: return "start_toy6";
        case 7: return "start_toy7";
        case 8: return "start_toy8";
        case 9: return "start_toy9";
        case 10: return "start_toy10";
        case 11: return "start_toy11";
        case 12: return "start_toy12";
        case 13: return "start_toy13";
        case 14: return "start_toy14";
        case 15: return "start_toy15";
        case 16: return "start_toy16";
        default: return "start_toy";
    }
}

SimulationProfile MakeSimulationProfile(int preset_id,
                                        std::size_t total_nodes,
                                        bool walled_mode) {
    SimulationProfile profile;
    profile.preset_name = PresetName(preset_id);

    if (total_nodes <= 300U) {
        profile.physics_iterations_per_frame = 4;
        profile.nodes_per_frame = 10;
        profile.theta = 0.70f;
        profile.intensity = 4.2f;
        profile.point_size = 6.6f;
        profile.line_width = 1.45f;
        profile.scene_scale = 2.9f;
        profile.fov_degrees = 37.0f;
    } else if (total_nodes <= 1200U) {
        profile.physics_iterations_per_frame = 3;
        profile.nodes_per_frame = 14;
        profile.theta = 0.74f;
        profile.point_size = 6.0f;
        profile.line_width = 1.35f;
        profile.scene_scale = 2.65f;
        profile.fov_degrees = 38.0f;
    } else if (total_nodes <= 5000U) {
        profile.physics_iterations_per_frame = 2;
        profile.nodes_per_frame = 22;
        profile.theta = 0.80f;
        profile.point_size = 5.4f;
        profile.line_width = 1.22f;
        profile.scene_scale = 2.4f;
        profile.fov_degrees = 39.0f;
    } else if (total_nodes <= 15000U) {
        profile.physics_iterations_per_frame = 1;
        profile.nodes_per_frame = 32;
        profile.theta = 0.88f;
        profile.point_size = 4.8f;
        profile.line_width = 1.12f;
        profile.scene_scale = 2.18f;
        profile.fov_degrees = 40.0f;
    } else {
        profile.physics_iterations_per_frame = 1;
        profile.nodes_per_frame = 44;
        profile.theta = 0.94f;
        profile.point_size = 4.3f;
        profile.line_width = 1.02f;
        profile.scene_scale = 2.0f;
        profile.fov_degrees = 41.0f;
    }

    if (walled_mode) {
        profile.scene_scale += 0.12f;
        profile.theta = std::min(1.10f, profile.theta + 0.04f);
    }

    profile.orbit_speed = 24.0f;
    profile.damping = 0.95f;
    return profile;
}

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
            options.custom_physics_iterations_per_frame = true;
        } else if (arg == "--nodes-per-frame") {
            if (!ParseIntArg(require_value("--nodes-per-frame"), options.nodes_per_frame)) {
                throw std::runtime_error("Invalid --nodes-per-frame value");
            }
            options.custom_nodes_per_frame = true;
        } else if (arg == "--fps") {
            if (!ParseIntArg(require_value("--fps"), options.fps)) {
                throw std::runtime_error("Invalid --fps value");
            }
        } else if (arg == "--theta") {
            if (!ParseFloatArg(require_value("--theta"), options.theta)) {
                throw std::runtime_error("Invalid --theta value");
            }
            options.custom_theta = true;
        } else if (arg == "--damping") {
            if (!ParseFloatArg(require_value("--damping"), options.damping)) {
                throw std::runtime_error("Invalid --damping value");
            }
            options.custom_damping = true;
        } else if (arg == "--intensity") {
            if (!ParseFloatArg(require_value("--intensity"), options.intensity)) {
                throw std::runtime_error("Invalid --intensity value");
            }
            options.custom_intensity = true;
        } else if (arg == "--point-size") {
            if (!ParseFloatArg(require_value("--point-size"), options.point_size)) {
                throw std::runtime_error("Invalid --point-size value");
            }
            options.custom_point_size = true;
        } else if (arg == "--line-width") {
            if (!ParseFloatArg(require_value("--line-width"), options.line_width)) {
                throw std::runtime_error("Invalid --line-width value");
            }
            options.custom_line_width = true;
        } else if (arg == "--scene-scale") {
            if (!ParseFloatArg(require_value("--scene-scale"), options.scene_scale)) {
                throw std::runtime_error("Invalid --scene-scale value");
            }
            options.custom_scene_scale = true;
        } else if (arg == "--orbit-speed") {
            if (!ParseFloatArg(require_value("--orbit-speed"), options.orbit_speed)) {
                throw std::runtime_error("Invalid --orbit-speed value");
            }
            options.custom_orbit_speed = true;
        } else if (arg == "--fov") {
            if (!ParseFloatArg(require_value("--fov"), options.fov_degrees)) {
                throw std::runtime_error("Invalid --fov value");
            }
            options.custom_fov_degrees = true;
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
                << "  --orbit-speed <float>\n"
                << "  --fov <float>\n"
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
    options.orbit_speed = std::clamp(options.orbit_speed, 0.0f, 120.0f);
    options.fov_degrees = std::clamp(options.fov_degrees, 20.0f, 80.0f);
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
        const graph::SimulationProfile profile =
            graph::MakeSimulationProfile(options.preset_id, graph_data.adjacency.size(), puzzle.walled_mode);

        const int physics_iterations_per_frame =
            options.custom_physics_iterations_per_frame ? options.physics_iterations_per_frame : profile.physics_iterations_per_frame;
        const int nodes_per_frame =
            options.custom_nodes_per_frame ? options.nodes_per_frame : profile.nodes_per_frame;
        const float theta = options.custom_theta ? options.theta : profile.theta;
        const float damping = options.custom_damping ? options.damping : profile.damping;
        const float intensity = options.custom_intensity ? options.intensity : profile.intensity;
        const float point_size = options.custom_point_size ? options.point_size : profile.point_size;
        const float line_width = options.custom_line_width ? options.line_width : profile.line_width;
        const float scene_scale = options.custom_scene_scale ? options.scene_scale : profile.scene_scale;
        const float orbit_speed = options.custom_orbit_speed ? options.orbit_speed : profile.orbit_speed;
        const float fov_degrees = options.custom_fov_degrees ? options.fov_degrees : profile.fov_degrees;

        std::cout << "Preset profile: " << profile.preset_name
                  << " | nodes=" << graph_data.adjacency.size()
                  << " | nodes/frame=" << nodes_per_frame
                  << " | physics iters/frame=" << physics_iterations_per_frame
                  << " | point size=" << point_size
                  << " | scene scale=" << scene_scale
                  << " | orbit speed=" << orbit_speed << "\n";

        graph::BfsGrowthSimulation3D::Parameters sim_params;
        sim_params.k = 0.05f;
        sim_params.gravity = 0.01f;
        sim_params.damping = damping;
        sim_params.dt = 0.05f;
        sim_params.intensity = intensity;
        sim_params.repulsion_theta = theta;
        sim_params.repulsion_softening = 1e-4f;
        sim_params.max_velocity = 10.0f;
        sim_params.worker_threads = options.worker_threads;

        graph::BfsGrowthSimulation3D sim(graph_data, 0, sim_params);

        graph::GraphRendererSettings renderer_settings;
        renderer_settings.width = options.width;
        renderer_settings.height = options.height;
        renderer_settings.visible = options.visible;
        renderer_settings.line_width = line_width;
        renderer_settings.point_size = point_size;
        renderer_settings.orbit_speed = orbit_speed;
        renderer_settings.fov_degrees = fov_degrees;

        graph::GraphRenderer3D renderer(renderer_settings,
                                        std::string("C++ BFS Graph Growth - ") + profile.preset_name);
        if (!renderer.Initialize()) {
            return 1;
        }
        renderer.SetPointSize(point_size);
        renderer.SetLineWidth(line_width);

        if (!options.video_output.empty()) {
            renderer.RenderVideo(sim,
                                 options.video_output,
                                 options.viewing_duration,
                                 nodes_per_frame,
                                 physics_iterations_per_frame,
                                 options.fps,
                                 scene_scale);
        }

        if (!options.skip_interactive && options.visible && !renderer.ShouldClose()) {
            renderer.RunInteractive(sim,
                                    nodes_per_frame,
                                    physics_iterations_per_frame,
                                    scene_scale);
        }
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}

#include "GraphRender.hpp"

#include "VideoRecorder.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace graph {

namespace {

GLuint CompileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) {
        return shader;
    }

    GLint log_length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
    std::string log(static_cast<std::size_t>(std::max(0, log_length)), '\0');
    glGetShaderInfoLog(shader, log_length, nullptr, log.data());
    std::cerr << "Shader compile failed: " << log << "\n";
    glDeleteShader(shader);
    return 0;
}

GLuint CreateProgram(const char* vertex_source, const char* fragment_source) {
    const GLuint vs = CompileShader(GL_VERTEX_SHADER, vertex_source);
    if (vs == 0) {
        return 0;
    }
    const GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragment_source);
    if (fs == 0) {
        glDeleteShader(vs);
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_TRUE) {
        return program;
    }

    GLint log_length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    std::string log(static_cast<std::size_t>(std::max(0, log_length)), '\0');
    glGetProgramInfoLog(program, log_length, nullptr, log.data());
    std::cerr << "Program link failed: " << log << "\n";
    glDeleteProgram(program);
    return 0;
}

void EnsureBufferCapacity(GLenum target,
                          std::size_t required_bytes,
                          std::size_t& capacity_bytes,
                          GLenum usage) {
    if (required_bytes == 0) {
        return;
    }

    if (required_bytes > capacity_bytes) {
        capacity_bytes = std::max(required_bytes, capacity_bytes == 0 ? required_bytes : capacity_bytes * 2);
        glBufferData(target,
                     static_cast<GLsizeiptr>(capacity_bytes),
                     nullptr,
                     usage);
    }
}

float UpdateCameraDistanceHybrid(const std::vector<float>& points,
                                 float current_distance,
                                 float min_distance,
                                 float max_distance) {
    if (points.empty()) {
        return current_distance;
    }

    const std::size_t n = points.size() / 3U;
    Vec3 centroid{};
    Vec3 min_p{points[0], points[1], points[2]};
    Vec3 max_p = min_p;

    for (std::size_t i = 0; i < n; ++i) {
        const Vec3 p{points[i * 3 + 0], points[i * 3 + 1], points[i * 3 + 2]};
        centroid += p;
        min_p.x = std::min(min_p.x, p.x);
        min_p.y = std::min(min_p.y, p.y);
        min_p.z = std::min(min_p.z, p.z);
        max_p.x = std::max(max_p.x, p.x);
        max_p.y = std::max(max_p.y, p.y);
        max_p.z = std::max(max_p.z, p.z);
    }
    centroid = centroid / static_cast<float>(n);

    float max_dist = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3 p{points[i * 3 + 0], points[i * 3 + 1], points[i * 3 + 2]};
        max_dist = std::max(max_dist, Length(p - centroid));
    }

    const float bbox_size = Length(max_p - min_p);
    const float scene_scale = std::max(max_dist, bbox_size * 0.9f);
    const float node_factor = std::log(static_cast<float>(n) + 1.0f) * 0.5f;
    const float ideal_distance = std::max(6.0f, scene_scale + node_factor);
    const float blended = current_distance * 0.88f + ideal_distance * 0.12f;
    return std::clamp(blended, min_distance, max_distance);
}

float EstimateSceneRadius(const std::vector<float>& points, const Vec3& center) {
    if (points.empty()) {
        return 1.0f;
    }

    const std::size_t n = points.size() / 3U;
    float max_radius = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3 p{points[i * 3 + 0], points[i * 3 + 1], points[i * 3 + 2]};
        max_radius = std::max(max_radius, Length(p - center));
    }
    return std::max(1.0f, max_radius);
}

}  // namespace

GraphRenderer3D::GraphRenderer3D(GraphRendererSettings settings, std::string title)
    : settings_(settings), title_(std::move(title)) {}

GraphRenderer3D::~GraphRenderer3D() {
    Shutdown();
}

bool GraphRenderer3D::Initialize() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return false;
    }

    glfwWindowHint(GLFW_VISIBLE, settings_.visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 16);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    window_ = glfwCreateWindow(settings_.width, settings_.height, title_.c_str(), nullptr, nullptr);
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

    const char* line_vertex_source = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        uniform mat4 uProj;
        uniform mat4 uView;
        void main() {
            gl_Position = uProj * uView * vec4(aPos, 1.0);
        }
    )";

    const char* line_fragment_source = R"(
        #version 330 core
        uniform vec4 uColor;
        out vec4 FragColor;
        void main() {
            FragColor = uColor;
        }
    )";

    const char* point_vertex_source = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        uniform mat4 uProj;
        uniform mat4 uView;
        uniform float uPointSize;
        out float vDepth;
        void main() {
            vec4 view_pos = uView * vec4(aPos, 1.0);
            gl_Position = uProj * view_pos;
            gl_PointSize = uPointSize;
            vDepth = max(-view_pos.z, 0.0);
        }
    )";

    const char* point_fragment_source = R"(
        #version 330 core
        uniform vec4 uColor;
        in float vDepth;
        out vec4 FragColor;
        void main() {
            vec2 uv = gl_PointCoord * 2.0 - 1.0;
            float r = length(uv);
            if (r > 1.0) {
                discard;
            }

            float edge = max(fwidth(r) * 1.75, 0.015);
            float alpha = 1.0 - smoothstep(1.0 - edge, 1.0 + edge, r);
            float core = 1.0 - smoothstep(0.0, 0.45, r);
            float halo = 1.0 - smoothstep(0.45, 0.92, r);
            float depth_fade = clamp(1.18 - 0.00035 * vDepth, 0.82, 1.10);
            vec3 color = mix(uColor.rgb * 0.84, vec3(1.0), core * 0.20);
            color += vec3(0.05, 0.09, 0.12) * halo * 0.22;
            color *= depth_fade;
            FragColor = vec4(color, alpha * uColor.a);
        }
    )";

    line_program_ = CreateProgram(line_vertex_source, line_fragment_source);
    point_program_ = CreateProgram(point_vertex_source, point_fragment_source);
    if (line_program_ == 0 || point_program_ == 0) {
        return false;
    }

    glGenVertexArrays(1, &point_vao_);
    glGenBuffers(1, &point_vbo_);
    glBindVertexArray(point_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, point_vbo_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(float) * 3), nullptr);
    glEnableVertexAttribArray(0);

    glGenVertexArrays(1, &edge_vao_);
    glGenBuffers(1, &edge_ebo_);
    glBindVertexArray(edge_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, point_vbo_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(float) * 3), nullptr);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, edge_ebo_);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glHint(GL_FRAGMENT_SHADER_DERIVATIVE_HINT, GL_NICEST);
    glPointSize(settings_.point_size);
    glLineWidth(settings_.line_width);

    int fb_width = settings_.width;
    int fb_height = settings_.height;
    glfwGetFramebufferSize(window_, &fb_width, &fb_height);
    glViewport(0, 0, fb_width, fb_height);
    last_time_ = glfwGetTime();
    return true;
}

bool GraphRenderer3D::ShouldClose() const {
    return window_ == nullptr || glfwWindowShouldClose(window_);
}

void GraphRenderer3D::PollEvents() {
    glfwPollEvents();
    if (window_ && glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}

void GraphRenderer3D::SetPointSize(float point_size) {
    settings_.point_size = std::clamp(point_size, 1.0f, 32.0f);
    if (window_) {
        glPointSize(settings_.point_size);
    }
}

void GraphRenderer3D::SetLineWidth(float line_width) {
    settings_.line_width = std::clamp(line_width, 1.0f, 6.0f);
    if (window_) {
        glLineWidth(settings_.line_width);
    }
}

void GraphRenderer3D::ReserveGeometryBuffers(std::size_t max_points, std::size_t max_edge_indices) {
    if (!window_) {
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, point_vbo_);
    EnsureBufferCapacity(GL_ARRAY_BUFFER,
                         max_points * sizeof(float) * 3U,
                         point_buffer_capacity_bytes_,
                         GL_STREAM_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, edge_ebo_);
    EnsureBufferCapacity(GL_ELEMENT_ARRAY_BUFFER,
                         max_edge_indices * sizeof(unsigned int),
                         edge_buffer_capacity_bytes_,
                         GL_DYNAMIC_DRAW);
    uploaded_edge_index_count_ = 0;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void GraphRenderer3D::UpdatePointGeometry(const std::vector<float>& point_vertices) {
    point_count_ = static_cast<GLsizei>(point_vertices.size() / 3U);
    last_points_ = point_vertices;

    glBindBuffer(GL_ARRAY_BUFFER, point_vbo_);
    const std::size_t bytes = point_vertices.size() * sizeof(float);
    EnsureBufferCapacity(GL_ARRAY_BUFFER, bytes, point_buffer_capacity_bytes_, GL_STREAM_DRAW);
    if (bytes > 0) {
        glBufferSubData(GL_ARRAY_BUFFER,
                        0,
                        static_cast<GLsizeiptr>(bytes),
                        point_vertices.data());
    }
}

void GraphRenderer3D::UpdateEdgeIndexBuffer(const std::vector<unsigned int>& edge_indices) {
    edge_index_count_ = static_cast<GLsizei>(edge_indices.size());
    glBindVertexArray(edge_vao_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, edge_ebo_);
    const std::size_t bytes = edge_indices.size() * sizeof(unsigned int);
    EnsureBufferCapacity(GL_ELEMENT_ARRAY_BUFFER, bytes, edge_buffer_capacity_bytes_, GL_DYNAMIC_DRAW);

    if (bytes > 0) {
        if (uploaded_edge_index_count_ > edge_index_count_) {
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,
                            0,
                            static_cast<GLsizeiptr>(bytes),
                            edge_indices.data());
        } else if (uploaded_edge_index_count_ < edge_index_count_) {
            const std::size_t offset_indices = static_cast<std::size_t>(uploaded_edge_index_count_);
            const std::size_t delta_indices = static_cast<std::size_t>(edge_index_count_ - uploaded_edge_index_count_);
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,
                            static_cast<GLintptr>(offset_indices * sizeof(unsigned int)),
                            static_cast<GLsizeiptr>(delta_indices * sizeof(unsigned int)),
                            edge_indices.data() + offset_indices);
        }
    }
    uploaded_edge_index_count_ = edge_index_count_;
    glBindVertexArray(0);
}

void GraphRenderer3D::RenderFrame(std::size_t active_nodes, std::size_t total_nodes) {
    const double now = glfwGetTime();
    const float dt = static_cast<float>(now - last_time_);
    last_time_ = now;

    if (!dragging_) {
        yaw_ += settings_.orbit_speed * dt;
    }
    UpdateAutoCamera();

    int fb_width = settings_.width;
    int fb_height = settings_.height;
    if (window_) {
        glfwGetFramebufferSize(window_, &fb_width, &fb_height);
    }

    glClearColor(0.035f, 0.042f, 0.072f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const Vec3 eye{
        target_.x + distance_ * std::cos(yaw_ * kDegToRad) * std::cos(pitch_ * kDegToRad),
        target_.y + distance_ * std::sin(pitch_ * kDegToRad),
        target_.z + distance_ * std::sin(yaw_ * kDegToRad) * std::cos(pitch_ * kDegToRad),
    };
    const float scene_radius = EstimateSceneRadius(last_points_, target_);
    const float z_near = std::max(0.22f, distance_ - scene_radius * 1.55f);
    const float z_far = std::max(z_near + 40.0f, distance_ + scene_radius * 2.75f + 60.0f);

    const Mat4 view = LookAt(eye, target_, {0.0f, 1.0f, 0.0f});
    const Mat4 proj = Perspective(settings_.fov_degrees * kDegToRad,
                                  static_cast<float>(std::max(1, fb_width)) / static_cast<float>(std::max(1, fb_height)),
                                  z_near,
                                  z_far);

    if (edge_index_count_ > 0) {
        glUseProgram(line_program_);
        glUniformMatrix4fv(glGetUniformLocation(line_program_, "uView"), 1, GL_FALSE, view.v.data());
        glUniformMatrix4fv(glGetUniformLocation(line_program_, "uProj"), 1, GL_FALSE, proj.v.data());
        glUniform4f(glGetUniformLocation(line_program_, "uColor"), 0.86f, 0.88f, 0.95f, 0.44f);
        glBindVertexArray(edge_vao_);
        glDrawElements(GL_LINES, edge_index_count_, GL_UNSIGNED_INT, nullptr);
    }

    if (point_count_ > 0) {
        glUseProgram(point_program_);
        glUniformMatrix4fv(glGetUniformLocation(point_program_, "uView"), 1, GL_FALSE, view.v.data());
        glUniformMatrix4fv(glGetUniformLocation(point_program_, "uProj"), 1, GL_FALSE, proj.v.data());
        glUniform1f(glGetUniformLocation(point_program_, "uPointSize"), settings_.point_size);
        glUniform4f(glGetUniformLocation(point_program_, "uColor"), 0.20f, 0.84f, 1.0f, 0.96f);
        glBindVertexArray(point_vao_);
        glDrawArrays(GL_POINTS, 0, point_count_);
    }

    glBindVertexArray(0);
    glfwSwapBuffers(window_);

    if ((frame_counter_ % 240U) == 0U && window_) {
        std::ostringstream oss;
        oss << title_ << " | Nodes " << active_nodes << "/" << total_nodes
            << " | Edges " << (edge_index_count_ / 2);
        glfwSetWindowTitle(window_, oss.str().c_str());
    }
    ++frame_counter_;
}

void GraphRenderer3D::RunInteractive(BfsGrowthSimulation3D& sim,
                                     int nodes_per_frame,
                                     int physics_iterations_per_frame,
                                     float scene_scale) {
    std::vector<float> point_vertices;
    point_vertices.reserve(sim.TotalNodeCount() * 3U);

    while (!ShouldClose()) {
        PollEvents();

        const int smooth_budget = 4 + static_cast<int>(sim.ActiveNodeCount() / 800U);
        const int add_budget = std::min(std::max(1, nodes_per_frame), smooth_budget);
        for (int i = 0; i < add_budget; ++i) {
            if (!sim.AddNextBfsNode()) {
                break;
            }
        }

        if (!sim.GrowthFinished() || sim.MaxVelocityMagnitude() > 0.04f) {
            sim.StepPhysics(std::max(1, physics_iterations_per_frame));
        }

        sim.BuildPointBuffer(point_vertices, scene_scale);
        UpdatePointGeometry(point_vertices);
        if (sim.ConsumeEdgeIndexBufferDirty()) {
            UpdateEdgeIndexBuffer(sim.EdgeIndices());
        }
        RenderFrame(sim.ActiveNodeCount(), sim.TotalNodeCount());
    }
}

void GraphRenderer3D::RenderVideo(BfsGrowthSimulation3D& sim,
                                  const std::string& output_path,
                                  float viewing_duration,
                                  int nodes_per_frame,
                                  int physics_iterations_per_frame,
                                  int fps,
                                  float scene_scale) {
    int fb_width = settings_.width;
    int fb_height = settings_.height;
    if (window_) {
        glfwGetFramebufferSize(window_, &fb_width, &fb_height);
    }

    AutoVideoRecorder recorder(fps);
    recorder.StartAutoRender(output_path,
                             fb_width,
                             fb_height,
                             sim.TotalNodeCount(),
                             viewing_duration,
                             nodes_per_frame);

    std::vector<float> point_vertices;
    point_vertices.reserve(sim.TotalNodeCount() * 3U);
    while (!ShouldClose() && recorder.IsRecording()) {
        PollEvents();
        recorder.Update(1.0f / static_cast<float>(std::max(1, fps)), sim.ActiveNodeCount());

        if (recorder.ShouldAddNodes()) {
            const int smooth_budget = 4 + static_cast<int>(sim.ActiveNodeCount() / 800U);
            const int add_budget = std::min(recorder.NodesPerFrame(), smooth_budget);
            for (int i = 0; i < add_budget; ++i) {
                if (!sim.AddNextBfsNode()) {
                    break;
                }
            }
        }

        if (recorder.Phase() == RenderPhase::Growth || sim.MaxVelocityMagnitude() > 0.04f) {
            sim.StepPhysics(std::max(1, physics_iterations_per_frame));
        }

        sim.BuildPointBuffer(point_vertices, scene_scale);
        UpdatePointGeometry(point_vertices);
        if (sim.ConsumeEdgeIndexBufferDirty()) {
            UpdateEdgeIndexBuffer(sim.EdgeIndices());
        }
        RenderFrame(sim.ActiveNodeCount(), sim.TotalNodeCount());
        recorder.CaptureFrame();

        if ((recorder.TotalFrames() % 60U) == 0U) {
            if (recorder.Phase() == RenderPhase::Growth) {
                std::cout << "Recording growth: " << sim.ActiveNodeCount()
                          << "/" << sim.TotalNodeCount() << " nodes\n";
            } else {
                std::cout << "Recording viewing phase, remaining "
                          << std::fixed << std::setprecision(2)
                          << recorder.ViewingDurationRemaining() << "s\n";
            }
        }
    }

    recorder.Stop();
    std::cout << "Saved frame sequence to " << recorder.OutputDirectory().string() << "\n";
}

void GraphRenderer3D::Close() {
    Shutdown();
}

void GraphRenderer3D::Shutdown() {
    if (point_vbo_ != 0) {
        glDeleteBuffers(1, &point_vbo_);
        point_vbo_ = 0;
    }
    if (point_vao_ != 0) {
        glDeleteVertexArrays(1, &point_vao_);
        point_vao_ = 0;
    }
    if (edge_ebo_ != 0) {
        glDeleteBuffers(1, &edge_ebo_);
        edge_ebo_ = 0;
    }
    if (edge_vao_ != 0) {
        glDeleteVertexArrays(1, &edge_vao_);
        edge_vao_ = 0;
    }
    if (line_program_ != 0) {
        glDeleteProgram(line_program_);
        line_program_ = 0;
    }
    if (point_program_ != 0) {
        glDeleteProgram(point_program_);
        point_program_ = 0;
    }
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

void GraphRenderer3D::UpdateAutoCamera() {
    if (last_points_.empty()) {
        return;
    }

    const std::size_t n = last_points_.size() / 3U;
    Vec3 centroid{};
    for (std::size_t i = 0; i < n; ++i) {
        centroid.x += last_points_[i * 3 + 0];
        centroid.y += last_points_[i * 3 + 1];
        centroid.z += last_points_[i * 3 + 2];
    }
    centroid = centroid / static_cast<float>(n);

    distance_ = UpdateCameraDistanceHybrid(last_points_, distance_, 0.35f, 10000.0f);
    target_ = target_ * 0.90f + centroid * 0.10f;
}

void GraphRenderer3D::OnFramebufferSize(int width, int height) {
    settings_.width = std::max(1, width);
    settings_.height = std::max(1, height);
    glViewport(0, 0, settings_.width, settings_.height);
}

void GraphRenderer3D::OnMouseButton(int button, int action) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        dragging_ = (action == GLFW_PRESS);
        if (dragging_) {
            glfwGetCursorPos(window_, &last_cursor_x_, &last_cursor_y_);
        }
    }
}

void GraphRenderer3D::OnCursorPos(double x, double y) {
    if (!dragging_) {
        return;
    }

    const double dx = x - last_cursor_x_;
    const double dy = y - last_cursor_y_;
    last_cursor_x_ = x;
    last_cursor_y_ = y;

    yaw_ += static_cast<float>(dx * 0.28);
    pitch_ -= static_cast<float>(dy * 0.28);
    pitch_ = std::clamp(pitch_, -89.0f, 89.0f);
}

void GraphRenderer3D::OnScroll(double yoffset) {
    distance_ -= static_cast<float>(yoffset);
    distance_ = std::max(distance_, 0.1f);
}

void GraphRenderer3D::FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto* self = reinterpret_cast<GraphRenderer3D*>(glfwGetWindowUserPointer(window));
    if (self) {
        self->OnFramebufferSize(width, height);
    }
}

void GraphRenderer3D::MouseButtonCallback(GLFWwindow* window, int button, int action, int /*mods*/) {
    auto* self = reinterpret_cast<GraphRenderer3D*>(glfwGetWindowUserPointer(window));
    if (self) {
        self->OnMouseButton(button, action);
    }
}

void GraphRenderer3D::CursorPosCallback(GLFWwindow* window, double x, double y) {
    auto* self = reinterpret_cast<GraphRenderer3D*>(glfwGetWindowUserPointer(window));
    if (self) {
        self->OnCursorPos(x, y);
    }
}

void GraphRenderer3D::ScrollCallback(GLFWwindow* window, double /*xoffset*/, double yoffset) {
    auto* self = reinterpret_cast<GraphRenderer3D*>(glfwGetWindowUserPointer(window));
    if (self) {
        self->OnScroll(yoffset);
    }
}

}  // namespace graph

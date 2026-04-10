#pragma once

#include "GraphCommon.hpp"
#include "GraphPhysics.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace graph {

struct GraphRendererSettings {
    int width = 1920;
    int height = 1080;
    bool visible = true;
    float line_width = 1.15f;
    float point_size = 4.8f;
    float orbit_speed = 24.0f;
    float fov_degrees = 40.0f;
};

class GraphRenderer3D {
  public:
    GraphRenderer3D(GraphRendererSettings settings, std::string title);
    ~GraphRenderer3D();

    [[nodiscard]] bool Initialize();
    [[nodiscard]] bool ShouldClose() const;

    void PollEvents();
    void SetPointSize(float point_size);
    void SetLineWidth(float line_width);
    void ReserveGeometryBuffers(std::size_t max_points, std::size_t max_edge_indices);

    void UpdatePointGeometry(const std::vector<float>& point_vertices);
    void UpdateEdgeIndexBuffer(const std::vector<unsigned int>& edge_indices);
    void RenderFrame(std::size_t active_nodes, std::size_t total_nodes);
    void RunInteractive(BfsGrowthSimulation3D& sim,
                        int nodes_per_frame,
                        int physics_iterations_per_frame,
                        float scene_scale);
    void RenderVideo(BfsGrowthSimulation3D& sim,
                     const std::string& output_path,
                     float viewing_duration,
                     int nodes_per_frame,
                     int physics_iterations_per_frame,
                     int fps,
                     float scene_scale);
    void Close();

  private:
    GLFWwindow* window_ = nullptr;
    GraphRendererSettings settings_{};
    std::string title_;

    GLuint point_program_ = 0;
    GLuint line_program_ = 0;
    GLuint point_vao_ = 0;
    GLuint point_vbo_ = 0;
    GLuint edge_vao_ = 0;
    GLuint edge_ebo_ = 0;
    GLsizei point_count_ = 0;
    GLsizei edge_index_count_ = 0;
    GLsizei uploaded_edge_index_count_ = 0;
    std::size_t point_buffer_capacity_bytes_ = 0;
    std::size_t edge_buffer_capacity_bytes_ = 0;
    std::vector<float> last_points_;

    Vec3 target_{0.0f, 0.0f, 0.0f};
    float distance_ = 18.0f;
    float yaw_ = -90.0f;
    float pitch_ = 10.0f;
    bool dragging_ = false;
    double last_cursor_x_ = 0.0;
    double last_cursor_y_ = 0.0;
    double last_time_ = 0.0;
    std::uint64_t frame_counter_ = 0;

    void Shutdown();
    void UpdateAutoCamera();
    void OnFramebufferSize(int width, int height);
    void OnMouseButton(int button, int action);
    void OnCursorPos(double x, double y);
    void OnScroll(double yoffset);

    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void CursorPosCallback(GLFWwindow* window, double x, double y);
    static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
};

}  // namespace graph

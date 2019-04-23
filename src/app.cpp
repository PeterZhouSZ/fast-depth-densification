#include "util.hpp"
#include "gl-imgui.hpp"
#include "gl-geometry.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

// https://colmap.github.io/format.html
namespace colmap
{
    struct point_3d
    {
        uint32_t id{ -1 };
        float3 data;
    };

    struct pinhole_camera
    {

    };

    struct camera_view
    {

    };

    struct reconstruction
    {

    };
};

template <typename T>
class image_buffer
{
    uint2 dims{ 0, 0 };
    T * alias;
    struct delete_array { void operator()(T * p) { delete[] p; } };
    std::unique_ptr<T, decltype(image_buffer::delete_array())> buffer;
    uint32_t C{ 0 };

public:

    image_buffer() = default;

    image_buffer(const uint2 size, const uint32_t channels) :
        dims(size), C(channels), buffer(new T[size.x * size.y * channels], delete_array())
    {
        alias = buffer.get();
    }

    image_buffer(const image_buffer<T> & r) :
        dims(r.dims), buffer(new T[dims.x * dims.y * C], delete_array())
    {
        alias = buffer.get();
        if (r.alias) std::memcpy(alias, r.alias, dims.x * dims.y * C * sizeof(T));
    }

    image_buffer & operator=(const image_buffer<T> & r)
    {
        buffer = { new T[dims.x * dims.y * C], delete_array() };
        alias = buffer.get();
        dims = r.dims;
        C = r.C;
        if (r.alias) std::memcpy(alias, r.alias, dims.x * dims.y * C * sizeof(T));
        return *this;
    }

    void flip_y_inplace()
    {
        const size_t bytes_per_pixel = sizeof(float) * C;
        const size_t row_stride_bytes = dims.x * bytes_per_pixel;
        std::vector<uint8_t> row(row_stride_bytes);
        uint8_t * low = reinterpret_cast<uint8_t *>(alias);
        uint8_t * high = &reinterpret_cast<uint8_t *>(alias)[(dims.y - 1) * row_stride_bytes];

        for (; low < high; low += row_stride_bytes, high -= row_stride_bytes)
        {
            std::memcpy(row.data(), low, row_stride_bytes);
            std::memcpy(low, high, row_stride_bytes);
            std::memcpy(high, row.data(), row_stride_bytes);
        }
    }

    uint2 size() const { return dims; }
    uint32_t num_bytes() const { return C * dims.x * dims.y * sizeof(T); }
    uint32_t num_pixels() const { return dims.x * dims.y; }
    uint32_t num_channels() const { return C; }

    T * data() { return alias; }
    const T * data() const { return alias; }

    T & operator()(int y, int x) { return alias[y * dims.x + x]; }
    T & operator()(int y, int x, int channel) { return alias[C * (y * dims.x + x) + channel]; }

    const T operator()(int y, int x) const { return alias[y * dims.x + x]; }
    const T operator()(int y, int x, int channel) const { return alias[C * (y * dims.x + x) + channel]; }
};

typedef image_buffer<float> image_f;
typedef image_buffer<uint8_t> image_byte;

std::unique_ptr<window> win;
std::unique_ptr<gui::imgui_manager> imgui;

int main(int argc, char * argv[])
{
    try { win.reset(new window(1280, 720, "fast-depth-densification")); }
    catch (const std::exception & e) { std::cout << "Caught GLFW window exception: " << e.what() << std::endl; }
    
    imgui.reset(new gui::imgui_manager(win->get_glfw_window_handle()));
    gui::make_light_theme();

    simple_interactive_camera cam = {};
    cam.yfov = 1.33f;
    cam.near_clip = 0.001f;
    cam.far_clip = 256.0f;
    cam.position = { 0, 2.f, 5 };

    int2 windowSize = win->get_window_size();

    win->on_char = [&](int codepoint)
    {
        auto e = make_input_event(win->get_glfw_window_handle(), app_input_event::CHAR, win->get_cursor_pos(), 0);
        e.value[0] = codepoint;
        if (win->on_input) win->on_input(e);
    };

    win->on_key = [&](int key, int action, int mods)
    {
        auto e = make_input_event(win->get_glfw_window_handle(), app_input_event::KEY, win->get_cursor_pos(), action);
        e.value[0] = key;
        if (win->on_input) win->on_input(e);
    };

    win->on_mouse_button = [&](int button, int action, int mods)
    {
        auto e = make_input_event(win->get_glfw_window_handle(), app_input_event::MOUSE, win->get_cursor_pos(), action);
        e.value[0] = button;
        if (win->on_input) win->on_input(e);
    };

    win->on_cursor_pos = [&](linalg::aliases::float2 position)
    {
        auto e = make_input_event(win->get_glfw_window_handle(), app_input_event::CURSOR, position, 0);
        if (win->on_input) win->on_input(e);
    };

    float2 lastCursor;
    win->on_input = [&](const app_input_event & event)
    {
        imgui->update_input(event);
        cam.update_input(event);

        if (event.type == app_input_event::KEY)
        {
            if (event.value[0] == GLFW_KEY_ESCAPE) win->close();
        }
    };

    int width, height;
    glfwGetWindowSize(win->get_glfw_window_handle(), &width, &height);
    
    auto t0 = std::chrono::high_resolution_clock::now();
    while (!win->should_close())
    {
        glfwMakeContextCurrent(win->get_glfw_window_handle());
        glfwPollEvents();

        auto t1 = std::chrono::high_resolution_clock::now();
        float timestep = std::chrono::duration<float>(t1 - t0).count();
        t0 = t1;

        cam.update(timestep);

        glViewport(0, 0, width, height);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(1.f, 1.f, 1.f, 1.f);

        const float3 eye = cam.position;
        const float4x4 projectionMatrix = cam.get_projection_matrix((float)windowSize.x / (float)windowSize.y);
        const float4x4 viewMatrix = cam.get_view_matrix();
        const float4x4 viewProjMatrix = mul(projectionMatrix, viewMatrix);

        imgui->begin_frame();
        gui::imgui_fixed_window_begin("gfx-app", { { 0, 0 },{ 300, windowSize.y } });
        ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        gui::imgui_fixed_window_end();
        imgui->end_frame();

        win->swap_buffers();
    }

    imgui.reset();
    return EXIT_SUCCESS;
}

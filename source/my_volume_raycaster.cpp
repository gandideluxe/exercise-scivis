// -----------------------------------------------------------------------------
// Copyright  : (C) 2014 Andreas-C. Bernstein
//                  2015 Sebastian Thiele
// License    : MIT (see the file LICENSE)
// Maintainer : Sebastian Thiele <sebastian.thiele@uni-weimar.de>
// Stability  : experimantal exercise
//
// scivis exercise Example
// -----------------------------------------------------------------------------
#ifdef _MSC_VER
#pragma warning (disable: 4996)         // 'This function or variable may be unsafe': strcpy, strdup, sprintf, vsnprintf, sscanf, fopen
#endif

#define _USE_MATH_DEFINES
#include "fensterchen.hpp"
#include <string>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <stdexcept>
#include <cmath>

///GLM INCLUDES
#define GLM_FORCE_RADIANS
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/norm.hpp>

///PROJECT INCLUDES
#include <volume_loader_raw.hpp>
#include <transfer_function.hpp>
#include <utils.hpp>
#include <turntable.hpp>
#include <imgui.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>        // stb_image.h for PNG loading

const std::string g_file_vertex_shader("../../../source/shader/volume.vert");
const std::string g_file_fragment_shader("../../../source/shader/volume.frag");

GLuint loadShaders(std::string const& vs, std::string const& fs)
{
  std::string v = readFile(vs);
  std::string f = readFile(fs);
  return createProgram(v,f);
}

bool g_reload_shader_pressed                = false;
bool g_show_transfer_function               = false;
bool g_show_transfer_function_pressed       = false;
Turntable  g_turntable;

///SETUP VOLUME RAYCASTER HERE
// set the volume file
std::string g_file_string = "../../../data/head_w256_h256_d225_c1_b8.raw";

// set the sampling distance for the ray traversal
float       g_sampling_distance             = 0.001f;

float       g_iso_value                     = 0.2f;

// set the light position and color for shading
glm::vec3   g_light_pos                     = glm::vec3(1.0,  1.0,  1.0);
glm::vec3   g_light_color                   = glm::vec3(1.0f, 1.0f, 1.0f);

// set backgorund color here
//glm::vec3   g_background_color = glm::vec3(1.0f, 1.0f, 1.0f); //white
glm::vec3   g_background_color = glm::vec3(0.0f, 0.0f, 0.0f);   //black

glm::ivec2  g_window_res                    = glm::ivec2(600, 600);
Window g_win(g_window_res);

// imgui variables
static GLuint fontTex;
static bool mousePressed[2] = { false, false };

static int shader_handle, vert_handle, frag_handle;
static int texture_location, ortho_location;
static int position_location, uv_location, colour_location;
static size_t vbo_max_size = 20000;
static unsigned int vbo_handle, vao_handle;

struct Manipulator
{
  Manipulator()
    : m_turntable()
    , m_mouse_button_pressed(0,0,0)
    , m_mouse(0.0f,0.0f)
    , m_lastMouse(0.0f,0.0f)
    {}

  glm::mat4 matrix(Window const& g_win)
  {
    m_mouse = g_win.mousePosition();
    if (g_win.isButtonPressed(Window::MOUSE_BUTTON_LEFT)) {
      if (!m_mouse_button_pressed[0]) {
        m_mouse_button_pressed[0] = 1;
      }
      m_turntable.rotate(m_lastMouse, m_mouse);
      m_slideMouse = m_mouse;
      m_slidelastMouse = m_lastMouse;
    } else {
      m_mouse_button_pressed[0] = 0;
      m_turntable.rotate(m_slidelastMouse, m_slideMouse);
      //m_slideMouse *= 0.99f;
      //m_slidelastMouse *= 0.99f;
    }

    if (g_win.isButtonPressed(Window::MOUSE_BUTTON_MIDDLE)) {
      if (!m_mouse_button_pressed[1]) {
        m_mouse_button_pressed[1] = 1;
      }
      m_turntable.pan(m_lastMouse, m_mouse);
    } else {
      m_mouse_button_pressed[1] = 0;
    }

    if (g_win.isButtonPressed(Window::MOUSE_BUTTON_RIGHT)) {
      if (!m_mouse_button_pressed[2]) {
        m_mouse_button_pressed[2] = 1;
      }
      m_turntable.zoom(m_lastMouse, m_mouse);
    } else {
      m_mouse_button_pressed[2] = 0;
    }

    m_lastMouse = m_mouse;    
    return m_turntable.matrix();
  }

private:
  Turntable  m_turntable;
  glm::ivec3 m_mouse_button_pressed;
  glm::vec2  m_mouse;
  glm::vec2  m_lastMouse;
  glm::vec2  m_slideMouse;
  glm::vec2  m_slidelastMouse;
};

// This is the main rendering function that you have to implement and provide to ImGui (via setting up 'RenderDrawListsFn' in the ImGuiIO structure)
// If text or lines are blurry when integrating ImGui in your engine:
// - try adjusting ImGui::GetIO().PixelCenterOffset to 0.0f or 0.5f
// - in your Render function, try translating your projection matrix by (0.5f,0.5f) or (0.375f,0.375f)
static void ImImpl_RenderDrawLists(ImDrawList** const cmd_lists, int cmd_lists_count)
{
    if (cmd_lists_count == 0)
        return;

    // Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);

    // Setup texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fontTex);

    // Setup orthographic projection matrix
    const float width = ImGui::GetIO().DisplaySize.x;
    const float height = ImGui::GetIO().DisplaySize.y;
    const float ortho_projection[4][4] =
    {
        { 2.0f / width, 0.0f, 0.0f, 0.0f },
        { 0.0f, 2.0f / -height, 0.0f, 0.0f },
        { 0.0f, 0.0f, -1.0f, 0.0f },
        { -1.0f, 1.0f, 0.0f, 1.0f },
    };
    glUseProgram(shader_handle);
    glUniform1i(texture_location, 0);
    glUniformMatrix4fv(ortho_location, 1, GL_FALSE, &ortho_projection[0][0]);

    // Grow our buffer according to what we need
    size_t total_vtx_count = 0;
    for (int n = 0; n < cmd_lists_count; n++)
        total_vtx_count += cmd_lists[n]->vtx_buffer.size();
    glBindBuffer(GL_ARRAY_BUFFER, vbo_handle);
    size_t neededBufferSize = total_vtx_count * sizeof(ImDrawVert);
    if (neededBufferSize > vbo_max_size)
    {
        vbo_max_size = neededBufferSize + 5000;  // Grow buffer
        glBufferData(GL_ARRAY_BUFFER, vbo_max_size, NULL, GL_STREAM_DRAW);
    }

    // Copy and convert all vertices into a single contiguous buffer
    unsigned char* buffer_data = (unsigned char*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    if (!buffer_data)
        return;
    for (int n = 0; n < cmd_lists_count; n++)
    {
        const ImDrawList* cmd_list = cmd_lists[n];
        memcpy(buffer_data, &cmd_list->vtx_buffer[0], cmd_list->vtx_buffer.size() * sizeof(ImDrawVert));
        buffer_data += cmd_list->vtx_buffer.size() * sizeof(ImDrawVert);
    }
    glUnmapBuffer(GL_ARRAY_BUFFER);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(vao_handle);

    int cmd_offset = 0;
    for (int n = 0; n < cmd_lists_count; n++)
    {
        const ImDrawList* cmd_list = cmd_lists[n];
        int vtx_offset = cmd_offset;
        const ImDrawCmd* pcmd_end = cmd_list->commands.end();
        for (const ImDrawCmd* pcmd = cmd_list->commands.begin(); pcmd != pcmd_end; pcmd++)
        {
            glScissor((int)pcmd->clip_rect.x, (int)(height - pcmd->clip_rect.w), (int)(pcmd->clip_rect.z - pcmd->clip_rect.x), (int)(pcmd->clip_rect.w - pcmd->clip_rect.y));
            glDrawArrays(GL_TRIANGLES, vtx_offset, pcmd->vtx_count);
            vtx_offset += pcmd->vtx_count;
        }
        cmd_offset = vtx_offset;
    }

    // Restore modified state
    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_SCISSOR_TEST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static const char* ImImpl_GetClipboardTextFn()
{
    return glfwGetClipboardString(g_win.getGLFWwindow());
}

static void ImImpl_SetClipboardTextFn(const char* text)
{
    glfwSetClipboardString(g_win.getGLFWwindow(), text);
}


void InitImGui()
{
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = 1.0f / 60.0f;                                  // Time elapsed since last frame, in seconds (in this sample app we'll override this every frame because our timestep is variable)
    io.PixelCenterOffset = 0.5f;                                  // Align OpenGL texels
    io.KeyMap[ImGuiKey_Tab] = GLFW_KEY_TAB;                       // Keyboard mapping. ImGui will use those indices to peek into the io.KeyDown[] array.
    io.KeyMap[ImGuiKey_LeftArrow] = GLFW_KEY_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = GLFW_KEY_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = GLFW_KEY_UP;
    io.KeyMap[ImGuiKey_DownArrow] = GLFW_KEY_DOWN;
    io.KeyMap[ImGuiKey_Home] = GLFW_KEY_HOME;
    io.KeyMap[ImGuiKey_End] = GLFW_KEY_END;
    io.KeyMap[ImGuiKey_Delete] = GLFW_KEY_DELETE;
    io.KeyMap[ImGuiKey_Backspace] = GLFW_KEY_BACKSPACE;
    io.KeyMap[ImGuiKey_Enter] = GLFW_KEY_ENTER;
    io.KeyMap[ImGuiKey_Escape] = GLFW_KEY_ESCAPE;
    io.KeyMap[ImGuiKey_A] = GLFW_KEY_A;
    io.KeyMap[ImGuiKey_C] = GLFW_KEY_C;
    io.KeyMap[ImGuiKey_V] = GLFW_KEY_V;
    io.KeyMap[ImGuiKey_X] = GLFW_KEY_X;
    io.KeyMap[ImGuiKey_Y] = GLFW_KEY_Y;
    io.KeyMap[ImGuiKey_Z] = GLFW_KEY_Z;

    io.RenderDrawListsFn = ImImpl_RenderDrawLists;
    io.SetClipboardTextFn = ImImpl_SetClipboardTextFn;
    io.GetClipboardTextFn = ImImpl_GetClipboardTextFn;

    // Load font texture
    glGenTextures(1, &fontTex);
    glBindTexture(GL_TEXTURE_2D, fontTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    const void* png_data;
    unsigned int png_size;
    ImGui::GetDefaultFontData(NULL, NULL, &png_data, &png_size);
    int tex_x, tex_y, tex_comp;
    void* tex_data = stbi_load_from_memory((const unsigned char*)png_data, (int)png_size, &tex_x, &tex_y, &tex_comp, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_x, tex_y, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex_data);
    stbi_image_free(tex_data);
}

void UpdateImGui()
{
    ImGuiIO& io = ImGui::GetIO();

    // Setup resolution (every frame to accommodate for window resizing)
    int w, h;
    int display_w, display_h;
    glfwGetWindowSize(g_win.getGLFWwindow(), &w, &h);
    glfwGetFramebufferSize(g_win.getGLFWwindow(), &display_w, &display_h);
    io.DisplaySize = ImVec2((float)display_w, (float)display_h);                                   // Display size, in pixels. For clamping windows positions.

    // Setup time step
    static double time = 0.0f;
    const double current_time = glfwGetTime();
    io.DeltaTime = (float)(current_time - time);
    time = current_time;

    // Setup inputs
    // (we already got mouse wheel, keyboard keys & characters from glfw callbacks polled in glfwPollEvents())
    double mouse_x, mouse_y;
    glfwGetCursorPos(g_win.getGLFWwindow(), &mouse_x, &mouse_y);
    mouse_x *= (float)display_w / w;                                                               // Convert mouse coordinates to pixels
    mouse_y *= (float)display_h / h;
    io.MousePos = ImVec2((float)mouse_x, (float)mouse_y);                                          // Mouse position, in pixels (set to -1,-1 if no mouse / on another screen, etc.)
    io.MouseDown[0] = mousePressed[0] || glfwGetMouseButton(g_win.getGLFWwindow(), GLFW_MOUSE_BUTTON_LEFT) != 0;  // If a mouse press event came, always pass it as "mouse held this frame", so we don't miss click-release events that are shorter than 1 frame.
    io.MouseDown[1] = mousePressed[1] || glfwGetMouseButton(g_win.getGLFWwindow(), GLFW_MOUSE_BUTTON_RIGHT) != 0;

    // Start the frame
    ImGui::NewFrame();
}

int main(int argc, char* argv[])
{  
   //g_win = Window(g_window_res);
    InitImGui();

  // initialize the transfer function
  Transfer_function transfer_fun;
  
  // first clear possible old values
  transfer_fun.reset();

  // the add_stop method takes:
  //  - unsigned char or float - data value     (0.0 .. 1.0) or (0..255)
  //  - vec4f         - color and alpha value   (0.0 .. 1.0) per channel
  transfer_fun.add(0.0f, glm::vec4(0.0, 0.0, 0.0, 0.0));
  transfer_fun.add(1.0f, glm::vec4(1.0, 1.0, 1.0, 1.0));
   

  ///NOTHING TODO UNTIL HERE-------------------------------------------------------------------------------
  
  //init volume loader
  Volume_loader_raw loader;
  //read volume dimensions
  glm::ivec3 vol_dimensions = loader.get_dimensions(g_file_string);

  unsigned max_dim = std::max(std::max(vol_dimensions.x,
                            vol_dimensions.y),
                            vol_dimensions.z);

  // calculating max volume bounds of volume (0.0 .. 1.0)
  glm::vec3 max_volume_bounds = glm::vec3(vol_dimensions) / glm::vec3((float)max_dim);
  
  // loading volume file data
  auto volume_data = loader.load_volume(g_file_string);
  auto channel_size = loader.get_bit_per_channel(g_file_string) / 8;
  auto channel_count = loader.get_channel_count(g_file_string);
  
  // init and upload volume texture
  glActiveTexture(GL_TEXTURE0);
  createTexture3D(vol_dimensions.x, vol_dimensions.y, vol_dimensions.z, channel_size, channel_count, (char*)&volume_data[0]);

  // init and upload transfer function texture
  glActiveTexture(GL_TEXTURE1);
  createTexture2D(255u, 1u, (char*)&transfer_fun.get_RGBA_transfer_function_buffer()[0]);

  // setting up proxy geometry
  Cube cube(glm::vec3(0.0, 0.0, 0.0), max_volume_bounds);

  // loading actual raytracing shader code (volume.vert, volume.frag)
  // edit volume.frag to define the result of our volume raycaster
  GLuint program(0);
  try {
    program = loadShaders(g_file_vertex_shader, g_file_fragment_shader);
  } catch (std::logic_error& e) {
    std::cerr << e.what() << std::endl;
  }

  // init object manipulator (turntable)
  Manipulator manipulator;

  // manage keys here
  // add new input if neccessary (ie changing sampling distance, isovalues, ...)
  while (!g_win.shouldClose()) {
    // exit window with escape
    if (g_win.isKeyPressed(GLFW_KEY_ESCAPE)) {
      g_win.stop();
    }

    if (g_win.isKeyPressed(GLFW_KEY_LEFT)) {
        g_light_pos.x -= 0.5f;
    }

    if (g_win.isKeyPressed(GLFW_KEY_RIGHT)) {
        g_light_pos.x += 0.5f;
    }

    if (g_win.isKeyPressed(GLFW_KEY_UP)) {
        g_light_pos.z -= 0.5f;
    }

    if (g_win.isKeyPressed(GLFW_KEY_DOWN)) {
        g_light_pos.z += 0.5f;
    }

    if (g_win.isKeyPressed(GLFW_KEY_MINUS)) {
        g_iso_value -= 0.002f;
        g_iso_value = std::max(g_iso_value, 0.0f);
    }
    
    if (g_win.isKeyPressed(GLFW_KEY_EQUAL) || g_win.isKeyPressed(GLFW_KEY_KP_ADD)) {
        g_iso_value += 0.002f;
        g_iso_value = std::min(g_iso_value, 1.0f);
    }

    if (g_win.isKeyPressed(GLFW_KEY_D)) {
        g_sampling_distance -= 0.0001f;
        g_sampling_distance = std::max(g_sampling_distance, 0.0001f);
    }

    if (g_win.isKeyPressed(GLFW_KEY_S)) {
        g_sampling_distance += 0.0001f;
        g_sampling_distance = std::min(g_sampling_distance, 0.2f);
    }

    // to add key inputs:
    // check g_win.isKeyPressed(KEY_NAME)
    // - KEY_NAME - key name      (GLFW_KEY_A ... GLFW_KEY_Z)
    
    //if (g_win.isKeyPressed(GLFW_KEY_X)){
    //    
    //        ... do something
    //    
    //}

    /// reload shader if key R ist pressed
    if (g_win.isKeyPressed(GLFW_KEY_R)) {
        if (g_reload_shader_pressed != true) {
            GLuint newProgram(0);
            try {
                std::cout << "Reload shaders" << std::endl;
                newProgram = loadShaders(g_file_vertex_shader, g_file_fragment_shader);
            }
            catch (std::logic_error& e) {
                std::cerr << e.what() << std::endl;
                newProgram = 0;
            }
            if (0 != newProgram) {
                glDeleteProgram(program);
                program = newProgram;
            }
            g_reload_shader_pressed = true;
        }
    } else {
        g_reload_shader_pressed = false;
    }

    /// show transfer function if T is pressed
    if (g_win.isKeyPressed(GLFW_KEY_T)){
        if (!g_show_transfer_function_pressed){
            g_show_transfer_function = !g_show_transfer_function;
        }
        g_show_transfer_function_pressed = true;
    } else {
        g_show_transfer_function_pressed = false;
    }

    auto size = g_win.windowSize();
    glViewport(0, 0, size.x, size.y);
    glClearColor(g_background_color.x, g_background_color.y, g_background_color.z, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float fovy = 45.0f;
    float aspect = (float)size.x / (float)size.y;
    float zNear = 0.025f, zFar = 10.0f;
    glm::mat4 projection = glm::perspective(fovy, aspect, zNear, zFar);

    glm::vec3 translate = max_volume_bounds * glm::vec3(-0.5f);

    glm::vec3 eye = glm::vec3(0.0f, 0.0f, 1.5f);
    glm::vec3 target = glm::vec3(0.0f);
    glm::vec3 up(0.0f, 1.0f, 0.0f);

    auto view = glm::lookAt(eye, target, up);

    auto model_view = view
                    * manipulator.matrix(g_win)
                    // rotate head upright
                    * glm::rotate(0.5f*float(M_PI), glm::vec3(0.0f,1.0f,0.0f))
                    * glm::rotate(0.5f*float(M_PI), glm::vec3(1.0f,0.0f,0.0f))
                    * glm::translate(translate)
                    ;

    glm::vec4 camera_translate = glm::column(glm::inverse(model_view), 3);
    glm::vec3 camera_location = glm::vec3(camera_translate.x, camera_translate.y, camera_translate.z);

    camera_location /= glm::vec3(camera_translate.w);

    glm::vec4 light_location = glm::vec4(g_light_pos, 1.0f) * model_view;

    glUseProgram(program);

    glUniform1i(glGetUniformLocation(program, "volume_texture"), 0);
    glUniform1i(glGetUniformLocation(program, "transfer_texture"), 1);

    glUniform3fv(glGetUniformLocation(program, "camera_location"), 1,
        glm::value_ptr(camera_location));
    glUniform1f(glGetUniformLocation(program, "sampling_distance"), g_sampling_distance);
    glUniform1f(glGetUniformLocation(program, "iso_value"), g_iso_value);
    glUniform3fv(glGetUniformLocation(program, "max_bounds"), 1,
        glm::value_ptr(max_volume_bounds));
    glUniform3iv(glGetUniformLocation(program, "volume_dimensions"), 1,
        glm::value_ptr(vol_dimensions));

    glUniform3fv(glGetUniformLocation(program, "light_position"), 1,
        //glm::value_ptr(glm::vec3(light_location.x, light_location.y, light_location.z)));
        glm::value_ptr(g_light_pos));
    glUniform3fv(glGetUniformLocation(program, "light_color"), 1,
        glm::value_ptr(g_light_color));

    glUniform3fv(glGetUniformLocation(program, "light_color"), 1,
        glm::value_ptr(g_light_color));

    glUniformMatrix4fv(glGetUniformLocation(program, "Projection"), 1, GL_FALSE,
        glm::value_ptr(projection));
    glUniformMatrix4fv(glGetUniformLocation(program, "Modelview"), 1, GL_FALSE,
        glm::value_ptr(model_view));
    //cube.draw();
    glUseProgram(0);

    if (g_show_transfer_function)
        transfer_fun.update_and_draw();

    //g_win.update();

    //IMGUI ROUTINE begin
    ImGuiIO& io = ImGui::GetIO();
    io.MouseWheel = 0;
    mousePressed[0] = mousePressed[1] = false;
    glfwPollEvents();
    UpdateImGui();

    static bool show_test_window = true;
    static bool show_another_window = false;

    // 1. Show a simple window
    // Tip: if we don't call ImGui::Begin()/ImGui::End() the widgets appears in a window automatically called "Debug"
    {
        static float f;
        ImGui::Text("Hello, world!");
        ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
        show_test_window ^= ImGui::Button("Test Window");
        show_another_window ^= ImGui::Button("Another Window");

        // Calculate and show frame rate
        static float ms_per_frame[120] = { 0 };
        static int ms_per_frame_idx = 0;
        static float ms_per_frame_accum = 0.0f;
        ms_per_frame_accum -= ms_per_frame[ms_per_frame_idx];
        ms_per_frame[ms_per_frame_idx] = ImGui::GetIO().DeltaTime * 1000.0f;
        ms_per_frame_accum += ms_per_frame[ms_per_frame_idx];
        ms_per_frame_idx = (ms_per_frame_idx + 1) % 120;
        const float ms_per_frame_avg = ms_per_frame_accum / 120;
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", ms_per_frame_avg, 1000.0f / ms_per_frame_avg);
    }

    // 2. Show another simple window, this time using an explicit Begin/End pair
    if (show_another_window)
    {
        ImGui::Begin("Another Window", &show_another_window, ImVec2(200, 100));
        ImGui::Text("Hello");
        ImGui::End();
    }

    // 3. Show the ImGui test window. Most of the sample code is in ImGui::ShowTestWindow()
    if (show_test_window)
    {
        ImGui::SetNextWindowPos(ImVec2(650, 20), ImGuiSetCondition_FirstUseEver);
        ImGui::ShowTestWindow(&show_test_window);
    }

    // Rendering
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(0.8f, 0.6f, 0.6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui::Render();
    glfwSwapBuffers(g_win.getGLFWwindow());
    //IMGUI ROUTINE end

  }

  //IMGUI shutdown
  if (vao_handle) glDeleteVertexArrays(1, &vao_handle);
  if (vbo_handle) glDeleteBuffers(1, &vbo_handle);
  glDetachShader(shader_handle, vert_handle);
  glDetachShader(shader_handle, frag_handle);
  glDeleteShader(vert_handle);
  glDeleteShader(frag_handle);
  glDeleteProgram(shader_handle);
  //IMGUI shutdown end

  ImGui::Shutdown();

  return 0;
}

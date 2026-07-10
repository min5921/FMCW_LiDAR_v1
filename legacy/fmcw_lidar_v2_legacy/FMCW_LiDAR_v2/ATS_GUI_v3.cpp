// ATS_GUI_linux.cpp
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <algorithm>
#include <limits>
#include <cmath>
#include <string>
#include <cstdio>

#include <GLFW/glfw3.h>
#include <GL/gl.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include "acquire.h"
#include "shared_state.h"
#include "logger.h"

// ----------------------------------------
// GLFW error callback
// ----------------------------------------
static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// ----------------------------------------
// Main
// ----------------------------------------
int main(int, char**)
{
    // -----------------------------
    // GLFW / OpenGL init
    // -----------------------------
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    const char* glsl_version = "#version 330";

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1500, 950, "ATS GUI", nullptr, nullptr);
    if (window == nullptr) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // -----------------------------
    // ImGui / ImPlot init
    // -----------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // -----------------------------
    // Acquisition runtime
    // -----------------------------
    HANDLE boardHandle = nullptr;
    std::thread acqThread;
    std::atomic<bool> acqRunning{ false };

    // -----------------------------
    // MTI runtime
    // -----------------------------
    MTIDevice* mti = nullptr;
    std::atomic<bool> mtiReady{ false };
    std::atomic<bool> mtiRunning{ false };

    // 실제 포트에 맞게 수정
    // 예: /dev/ttyUSB0, /dev/ttyACM0
    const char* mtiPortName = "/dev/ttyUSB0";

    // -----------------------------------------
    // Stop 순서:
    // 1) MTI STOP
    // 2) ATS STOP
    // -----------------------------------------
    auto stop_and_join = [&]() {
        if (mtiRunning.load(std::memory_order_relaxed)) {
            if (MTI_Stop(mti)) {
                AddLog("MTI stopped.");
            }
            else {
                AddLog("MTI stop FAILED.");
            }
            mtiRunning.store(false, std::memory_order_relaxed);
        }

        g_stop_request.store(true, std::memory_order_relaxed);

        if (boardHandle)
            AlazarAbortAsyncRead(boardHandle);

        if (acqThread.joinable())
            acqThread.join();

        acqRunning.store(false, std::memory_order_relaxed);
        AddLog("ATS stopped.");
        };

    // -----------------------------
    // UI state
    // -----------------------------
    bool autoFitY = true;
    bool log_auto_scroll = true;

    bool heat_auto_scale = true;
    float heat_min = 0.0f;
    float heat_max = 1.0f;

    // -----------------------------
    // Main loop
    // -----------------------------
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // -------------------------
        // New frame
        // -------------------------
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("ATS Plot");

        // -------------------------
        // Board open / configure once
        // -------------------------
        if (!boardHandle) {
            boardHandle = AlazarGetBoardBySystemID(1, 1);
            if (!boardHandle) {
                ImGui::Text("Board open FAILED");
            }
            else {
                if (!ConfigureBoard(boardHandle)) {
                    ImGui::Text("ConfigureBoard FAILED");
                }
                else {
                    ImGui::Text("Board ready.");
                }
            }
        }

        // -------------------------
        // Top controls
        // -------------------------
        {
            if (!acqRunning.load(std::memory_order_relaxed) && boardHandle) {
                if (ImGui::Button("Start")) {

                    // 1) MTI init (최초 1회)
                    if (!mtiReady.load(std::memory_order_relaxed)) {
                        if (MTI_Init(mti, mtiPortName)) {
                            mtiReady.store(true, std::memory_order_relaxed);
                            AddLog("MTI initialized. port=%s", mtiPortName);
                        }
                        else {
                            AddLog("MTI init FAILED. port=%s", mtiPortName);
                        }
                    }

                    // 2) ATS start
                    g_stop_request.store(false, std::memory_order_relaxed);
                    acqRunning.store(true, std::memory_order_relaxed);

                    acqThread = std::thread([&]() {
                        AcquireData(boardHandle);
                        acqRunning.store(false, std::memory_order_relaxed);
                        AddLog("ATS acquire thread exited.");
                        });

                    AddLog("ATS acquisition started.");

                    // 3) MTI raster start
                    if (mtiReady.load(std::memory_order_relaxed)) {
                        if (MTI_StartLinearRaster(mti)) {
                            mtiRunning.store(true, std::memory_order_relaxed);
                            AddLog("MTI linear raster started.");
                        }
                        else {
                            mtiRunning.store(false, std::memory_order_relaxed);
                            AddLog("MTI linear raster start FAILED.");
                            stop_and_join();
                        }
                    }
                    else {
                        AddLog("MTI not ready.");
                        stop_and_join();
                    }
                }
            }
            else if (acqRunning.load(std::memory_order_relaxed)) {
                if (ImGui::Button("Stop")) {
                    stop_and_join();
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Re-init MTI")) {
                if (acqRunning.load(std::memory_order_relaxed) || mtiRunning.load(std::memory_order_relaxed)) {
                    AddLog("Stop system before MTI re-init.");
                }
                else {
                    if (mti) {
                        MTI_Shutdown(mti);
                        mti = nullptr;
                    }
                    mtiReady.store(false, std::memory_order_relaxed);
                    mtiRunning.store(false, std::memory_order_relaxed);

                    if (MTI_Init(mti, mtiPortName)) {
                        mtiReady.store(true, std::memory_order_relaxed);
                        AddLog("MTI re-initialized. port=%s", mtiPortName);
                    }
                    else {
                        AddLog("MTI re-init FAILED. port=%s", mtiPortName);
                    }
                }
            }

            ImGui::SameLine();
            ImGui::Text("ATS: %s",
                acqRunning.load(std::memory_order_relaxed) ? "RUNNING" : "STOP");

            ImGui::SameLine();
            ImGui::Text("MTI: %s",
                mtiRunning.load(std::memory_order_relaxed) ? "RUNNING" : "STOP");

            ImGui::SameLine();
            ImGui::Text("READY: %s",
                mtiReady.load(std::memory_order_relaxed) ? "YES" : "NO");

            int rmax = g_r_max.load(std::memory_order_relaxed);
            int r = g_pick_r.load(std::memory_order_relaxed);
            if (rmax > 0) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(320);
                if (ImGui::SliderInt("##r", &r, 0, rmax))
                    g_pick_r.store(r, std::memory_order_relaxed);
                ImGui::SameLine();
                ImGui::Text("Record index r");
            }

            ImGui::SameLine();
            ImGui::Checkbox("AutoFit Y", &autoFitY);

            uint64_t fid = g_frame_id.load(std::memory_order_relaxed);
            ImGui::SameLine();
            ImGui::Text("frame_id: %llu", (unsigned long long)fid);

            float fps_now = g_frame_fps.load(std::memory_order_relaxed);
            ImGui::SameLine();
            ImGui::Text("FPS: %.2f", fps_now);
        }

        ImGui::Separator();

        // -------------------------
        // Copy shared data once/frame
        // -------------------------
        static std::vector<float> local_time;
        {
            std::lock_guard<std::mutex> lk(g_plot_mtx);
            local_time = g_plot;
        }

        static std::vector<float> local_fft;
        {
            std::lock_guard<std::mutex> lk(g_fft_mtx);
            local_fft = g_fft_db;
        }

        static std::vector<float> local_peak_val;
        static std::vector<float> local_peak_idx;
        {
            std::lock_guard<std::mutex> lk(g_peak_mtx);
            local_peak_val = g_peak_val_db;
            local_peak_idx = g_peak_idx_f;
        }

        static std::vector<float> local_dist;
        static std::vector<float> local_velo;
        {
            std::lock_guard<std::mutex> lk(g_distvelo_mtx);
            local_dist = g_dist_val;
            local_velo = g_velo_val;
        }

        static std::vector<float> local_heatmap;
        int heat_w = g_heat_w.load(std::memory_order_relaxed);
        int heat_h = g_heat_h.load(std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lk(g_heat_mtx);
            local_heatmap = g_heatmap;
        }

        static std::vector<float> batch_x;
        int peak_count = g_peak_count.load(std::memory_order_relaxed);
        if (peak_count > 0) {
            if ((int)batch_x.size() != peak_count) {
                batch_x.resize(peak_count);
                for (int i = 0; i < peak_count; ++i)
                    batch_x[i] = (float)i;
            }
        }
        else {
            batch_x.clear();
        }

        static std::vector<float> pair_x;
        int distvelo_count = g_distvelo_count.load(std::memory_order_relaxed);
        if (distvelo_count > 0) {
            if ((int)pair_x.size() != distvelo_count) {
                pair_x.resize(distvelo_count);
                for (int i = 0; i < distvelo_count; ++i)
                    pair_x[i] = (float)i;
            }
        }
        else {
            pair_x.clear();
        }

        // -------------------------
        // Layout split
        // -------------------------
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float log_height = 180.0f;
        float plot_height = avail.y - log_height - 8.0f;
        if (plot_height < 100.0f)
            plot_height = 100.0f;

        ImGui::BeginChild("PlotArea", ImVec2(0, plot_height), false);

        if (ImGui::BeginTabBar("MainTabs"))
        {
            if (ImGui::BeginTabItem("Time Domain"))
            {
                ImVec2 tab_avail = ImGui::GetContentRegionAvail();

                if (!local_time.empty()) {
                    if (ImPlot::BeginPlot("Time Domain", ImVec2(-1, tab_avail.y - 10.0f))) {
                        if (autoFitY) {
                            ImPlot::SetupAxes(
                                "sample", "amp",
                                ImPlotAxisFlags_AutoFit,
                                ImPlotAxisFlags_AutoFit);
                        }
                        else {
                            ImPlot::SetupAxes(
                                "sample", "amp",
                                ImPlotAxisFlags_AutoFit,
                                ImPlotAxisFlags_None);
                            ImPlot::SetupAxisLimits(ImAxis_Y1, -1.0, 1.0, ImGuiCond_Always);
                        }

                        ImPlot::PlotLine("ChA", local_time.data(), (int)local_time.size());
                        ImPlot::EndPlot();
                    }
                }
                else {
                    ImGui::Text("No time-domain data yet.");
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("FFT"))
            {
                ImVec2 tab_avail = ImGui::GetContentRegionAvail();

                if (!local_fft.empty()) {
                    if (ImPlot::BeginPlot("FFT (r-th record, dB)", ImVec2(-1, tab_avail.y - 10.0f))) {
                        ImPlot::SetupAxes(
                            "bin", "dB",
                            ImPlotAxisFlags_AutoFit,
                            ImPlotAxisFlags_AutoFit);

                        ImPlot::PlotLine("Mag(dB)", local_fft.data(), (int)local_fft.size());
                        ImPlot::EndPlot();
                    }
                }
                else {
                    ImGui::Text("No FFT data yet.");
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Peak Analysis"))
            {
                float peak_threshold_db = g_peak_threshold_db.load(std::memory_order_relaxed);
                int peak_min_index = g_peak_min_index.load(std::memory_order_relaxed);
                int peak_max_index = g_peak_max_index.load(std::memory_order_relaxed);

                int fft_len_now = g_fft_len.load(std::memory_order_relaxed);
                int peak_idx_max = (fft_len_now > 0) ? (fft_len_now - 1) : 0;

                peak_min_index = std::clamp(peak_min_index, 0, peak_idx_max);
                peak_max_index = std::clamp(peak_max_index, 0, peak_idx_max);

                if (peak_max_index < peak_min_index) {
                    std::swap(peak_min_index, peak_max_index);
                }

                ImGui::SetNextItemWidth(220);
                if (ImGui::SliderFloat("Peak Threshold (dB)", &peak_threshold_db, -120.0f, 120.0f, "%.1f")) {
                    g_peak_threshold_db.store(peak_threshold_db, std::memory_order_relaxed);
                }

                ImGui::SameLine();

                ImGui::SetNextItemWidth(220);
                if (ImGui::SliderInt("Peak Min Index", &peak_min_index, 0, peak_idx_max)) {
                    if (peak_min_index > peak_max_index)
                        peak_max_index = peak_min_index;

                    g_peak_min_index.store(peak_min_index, std::memory_order_relaxed);
                    g_peak_max_index.store(peak_max_index, std::memory_order_relaxed);
                }

                ImGui::SameLine();

                ImGui::SetNextItemWidth(220);
                if (ImGui::SliderInt("Peak Max Index", &peak_max_index, 0, peak_idx_max)) {
                    if (peak_max_index < peak_min_index)
                        peak_min_index = peak_max_index;

                    g_peak_min_index.store(peak_min_index, std::memory_order_relaxed);
                    g_peak_max_index.store(peak_max_index, std::memory_order_relaxed);
                }

                ImGui::SameLine();
                ImGui::Text("Peak count: %d", peak_count);

                ImGui::Text("Search range: [%d, %d] / FFT bins: 0 ~ %d",
                    peak_min_index, peak_max_index, peak_idx_max);

                ImGui::Separator();

                ImVec2 tab_avail = ImGui::GetContentRegionAvail();
                float gap = 8.0f;
                float h_each = (tab_avail.y - gap) * 0.5f;
                if (h_each < 120.0f)
                    h_each = 120.0f;

                if (!local_peak_idx.empty() && !batch_x.empty() &&
                    local_peak_idx.size() == batch_x.size()) {

                    if (ImPlot::BeginPlot("Peak Index vs Batch", ImVec2(-1, h_each))) {
                        ImPlot::SetupAxes(
                            "batch", "peak idx",
                            ImPlotAxisFlags_AutoFit,
                            ImPlotAxisFlags_AutoFit);

                        ImPlot::PlotLine(
                            "PeakIdx",
                            batch_x.data(),
                            local_peak_idx.data(),
                            (int)local_peak_idx.size());

                        ImPlot::EndPlot();
                    }
                }
                else {
                    ImGui::Text("No peak-index data yet.");
                }

                ImGui::Spacing();

                if (!local_peak_val.empty() && !batch_x.empty() &&
                    local_peak_val.size() == batch_x.size()) {

                    if (ImPlot::BeginPlot("Peak Value vs Batch", ImVec2(-1, h_each))) {
                        ImPlot::SetupAxes(
                            "batch", "peak val (dB)",
                            ImPlotAxisFlags_AutoFit,
                            ImPlotAxisFlags_AutoFit);

                        ImPlot::PlotLine(
                            "PeakVal",
                            batch_x.data(),
                            local_peak_val.data(),
                            (int)local_peak_val.size());

                        ImPlot::EndPlot();
                    }
                }
                else {
                    ImGui::Text("No peak-value data yet.");
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Distance / Velocity"))
            {
                ImVec2 tab_avail = ImGui::GetContentRegionAvail();
                float gap = 8.0f;
                float h_each = (tab_avail.y - gap) * 0.5f;
                if (h_each < 120.0f)
                    h_each = 120.0f;

                if (!local_dist.empty() && !pair_x.empty() &&
                    local_dist.size() == pair_x.size()) {

                    if (ImPlot::BeginPlot("Distance (Z) vs Pair", ImVec2(-1, h_each))) {
                        ImPlot::SetupAxes(
                            "pair", "distance(z)",
                            ImPlotAxisFlags_AutoFit,
                            ImPlotAxisFlags_AutoFit);

                        ImPlot::PlotLine(
                            "DistanceZ",
                            pair_x.data(),
                            local_dist.data(),
                            (int)local_dist.size());

                        ImPlot::EndPlot();
                    }
                }
                else {
                    ImGui::Text("No distance data yet.");
                }

                ImGui::Spacing();

                if (!local_velo.empty() && !pair_x.empty() &&
                    local_velo.size() == pair_x.size()) {

                    if (ImPlot::BeginPlot("Velocity vs Pair", ImVec2(-1, h_each))) {
                        ImPlot::SetupAxes(
                            "pair", "velocity",
                            ImPlotAxisFlags_AutoFit,
                            ImPlotAxisFlags_AutoFit);

                        ImPlot::PlotLine(
                            "Velocity",
                            pair_x.data(),
                            local_velo.data(),
                            (int)local_velo.size());

                        ImPlot::EndPlot();
                    }
                }
                else {
                    ImGui::Text("No velocity data yet.");
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Heatmap"))
            {
                ImVec2 tab_avail = ImGui::GetContentRegionAvail();

                ImGui::Checkbox("Auto Scale", &heat_auto_scale);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(140);
                ImGui::InputFloat("Min", &heat_min, 0.0f, 0.0f, "%.4f");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(140);
                ImGui::InputFloat("Max", &heat_max, 0.0f, 0.0f, "%.4f");

                ImGui::Separator();

                if (!local_heatmap.empty() && heat_w > 0 && heat_h > 0)
                {
                    float vmin = heat_min;
                    float vmax = heat_max;

                    if (heat_auto_scale) {
                        vmin = std::numeric_limits<float>::max();
                        vmax = -std::numeric_limits<float>::max();

                        for (float v : local_heatmap) {
                            if (std::isnan(v)) continue;
                            vmin = std::min(vmin, v);
                            vmax = std::max(vmax, v);
                        }

                        if (!(vmin < vmax)) {
                            vmin = 0.0f;
                            vmax = 1.0f;
                        }

                        heat_min = vmin;
                        heat_max = vmax;
                    }

                    ImGui::BeginGroup();

                    if (ImPlot::BeginPlot("Z Heatmap", ImVec2(tab_avail.x - 90.0f, tab_avail.y - 40.0f)))
                    {
                        ImPlot::SetupAxes(
                            "X Pixel", "B Scan",
                            ImPlotAxisFlags_AutoFit,
                            ImPlotAxisFlags_AutoFit);

                        ImPlot::PushColormap(ImPlotColormap_Jet);

                        ImPlot::PlotHeatmap(
                            "Z",
                            local_heatmap.data(),
                            heat_h,
                            heat_w,
                            vmin,
                            vmax,
                            nullptr,
                            ImPlotPoint(0, 0),
                            ImPlotPoint((double)heat_w, (double)heat_h)
                        );

                        ImPlot::PopColormap();
                        ImPlot::EndPlot();
                    }

                    ImGui::EndGroup();
                    ImGui::SameLine();

                    ImPlot::PushColormap(ImPlotColormap_Jet);
                    ImPlot::ColormapScale("Z Scale", vmin, vmax, ImVec2(60, tab_avail.y - 40.0f));
                    ImPlot::PopColormap();
                }
                else {
                    ImGui::Text("No heatmap data yet.");
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::EndChild();

        ImGui::Spacing();

        ImGui::BeginChild("LogPanel", ImVec2(0, 0), true);

        if (ImGui::Button("Clear"))
            ClearLogs();

        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &log_auto_scroll);

        ImGui::Separator();

        ImGui::BeginChild("LogScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        int n = GetLogCount();
        for (int i = 0; i < n; ++i) {
            std::string line = GetLogLine(i);
            ImGui::TextUnformatted(line.c_str());
        }

        if (log_auto_scroll) {
            float y = ImGui::GetScrollY();
            float y_max = ImGui::GetScrollMaxY();
            if (y >= y_max - 2.0f)
                ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();
        ImGui::EndChild();

        ImGui::End();

        ImGui::Render();

        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // -----------------------------
    // Shutdown
    // -----------------------------
    stop_and_join();

    if (mti) {
        MTI_Shutdown(mti);
        mti = nullptr;
    }

    mtiReady.store(false, std::memory_order_relaxed);
    mtiRunning.store(false, std::memory_order_relaxed);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
// ATS_GUI.cpp
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <tchar.h>

#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <algorithm>
#include <limits>
#include <cmath>
#include <string>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "implot.h"

#include "acquire.h"
#include "shared_state.h"
#include "logger.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

static ID3D11Device* g_pd3dDevice = NULL;
static ID3D11DeviceContext* g_pd3dDeviceContext = NULL;
static IDXGISwapChain* g_pSwapChain = NULL;
static ID3D11RenderTargetView* g_mainRenderTargetView = NULL;

static void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = NULL;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

static void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = NULL;
    }
}

static void CleanupDeviceD3D()
{
    CleanupRenderTarget();

    if (g_pSwapChain) {
        g_pSwapChain->Release();
        g_pSwapChain = NULL;
    }
    if (g_pd3dDeviceContext) {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = NULL;
    }
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = NULL;
    }
}

static bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        0,
        NULL,
        0,
        D3D11_SDK_VERSION,
        &sd,
        &g_pSwapChain,
        &g_pd3dDevice,
        &featureLevel,
        &g_pd3dDeviceContext);

    if (hr != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(
                0,
                (UINT)LOWORD(lParam),
                (UINT)HIWORD(lParam),
                DXGI_FORMAT_UNKNOWN,
                0);
            CreateRenderTarget();
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // -----------------------------
    // Window class / create window
    // -----------------------------
    WNDCLASSEX wc = {
        sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L,
        GetModuleHandle(NULL), NULL, NULL, NULL, NULL,
        _T("ATS_GUI_CLASS"), NULL
    };

    RegisterClassEx(&wc);

    HWND hwnd = CreateWindow(
        wc.lpszClassName,
        _T("ATS GUI"),
        WS_OVERLAPPEDWINDOW,
        100, 100, 1500, 950,
        NULL, NULL, wc.hInstance, NULL);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // -----------------------------
    // ImGui / ImPlot init
    // -----------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // -----------------------------
    // Acquisition runtime
    // -----------------------------
    HANDLE boardHandle = nullptr;
    std::thread acqThread;
    std::atomic<bool> acqRunning{ false };

    auto stop_and_join = [&]() {
        g_stop_request.store(true);
        if (boardHandle)
            AlazarAbortAsyncRead(boardHandle);
        if (acqThread.joinable())
            acqThread.join();
        acqRunning.store(false);
        };

    // -----------------------------
    // UI state
    // -----------------------------
    bool autoFitY = true;
    bool log_auto_scroll = true;

    bool done = false;
    while (!done)
    {
        // -------------------------
        // Win32 message pump
        // -------------------------
        MSG msg;
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // -------------------------
        // New frame
        // -------------------------
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // =========================
        // Main window
        // =========================
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
            if (!acqRunning.load() && boardHandle) {
                if (ImGui::Button("Start")) {
                    g_stop_request.store(false);
                    acqRunning.store(true);
                    acqThread = std::thread([&]() {
                        AcquireData(boardHandle);
                        acqRunning.store(false);
                        });
                }
            }
            else if (acqRunning.load()) {
                if (ImGui::Button("Stop")) {
                    stop_and_join();
                }
            }

            ImGui::SameLine();
            ImGui::Text("running: %s", acqRunning.load() ? "YES" : "NO");

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

        // -------------------------
        // Layout split
        // -------------------------
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float log_height = 180.0f;
        float plot_height = avail.y - log_height - 8.0f;
        if (plot_height < 100.0f)
            plot_height = 100.0f;

        // =========================
        // Plot / Tab area
        // =========================
        ImGui::BeginChild("PlotArea", ImVec2(0, plot_height), false);

        if (ImGui::BeginTabBar("MainTabs"))
        {
            // =====================================================
            // TAB 1: Time Domain
            // =====================================================
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

            // =====================================================
            // TAB 2: FFT
            // =====================================================
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

            // =====================================================
            // TAB 3: Peak Analysis
            // =====================================================
            if (ImGui::BeginTabItem("Peak Analysis"))
            {
                float peak_threshold_db = g_peak_threshold_db.load(std::memory_order_relaxed);
                int peak_min_index = g_peak_min_index.load(std::memory_order_relaxed);

                int fft_len_now = g_fft_len.load(std::memory_order_relaxed);
                int peak_idx_max = (fft_len_now > 0) ? (fft_len_now - 1) : 0;

                peak_min_index = std::clamp(peak_min_index, 0, peak_idx_max);

                ImGui::SetNextItemWidth(260);
                if (ImGui::SliderFloat("Peak Threshold (dB)", &peak_threshold_db, -120.0f, 120.0f, "%.1f")) {
                    g_peak_threshold_db.store(peak_threshold_db, std::memory_order_relaxed);
                }

                ImGui::SameLine();

                ImGui::SetNextItemWidth(260);
                if (ImGui::SliderInt("Peak Min Index", &peak_min_index, 0, peak_idx_max)) {
                    g_peak_min_index.store(peak_min_index, std::memory_order_relaxed);
                }

                ImGui::SameLine();
                ImGui::Text("Peak count: %d", peak_count);

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

            ImGui::EndTabBar();
        }

        ImGui::EndChild(); // PlotArea

        ImGui::Spacing();

        // =========================
        // Log panel (always bottom)
        // =========================
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

        ImGui::EndChild(); // LogScroll
        ImGui::EndChild(); // LogPanel

        ImGui::End(); // ATS Plot

        // =========================
        // Render
        // =========================
        ImGui::Render();
        const float clear_color[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    // -----------------------------
    // Shutdown
    // -----------------------------
    stop_and_join();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);

    return 0;
}
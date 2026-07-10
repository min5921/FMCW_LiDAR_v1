// shared_state.h
#pragma once

#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>

extern std::vector<float> g_plot;
extern std::mutex g_plot_mtx;

extern std::vector<float> g_fft_db;
extern std::mutex g_fft_mtx;

extern std::vector<float> g_peak_val_db;
extern std::vector<float> g_peak_idx_f;
extern std::vector<float> g_peak_hit_f;
extern std::mutex g_peak_mtx;

extern std::atomic<int> g_r_max;
extern std::atomic<int> g_pick_r;
extern std::atomic<int> g_plot_len;
extern std::atomic<int> g_plot_len_max;
extern std::atomic<int> g_fft_len;
extern std::atomic<uint64_t> g_frame_id;
extern std::atomic<bool> g_stop_request;

extern std::atomic<int> g_peak_count;
extern std::atomic<int> g_peak_min_index;
extern std::atomic<int> g_peak_max_index;
extern std::atomic<float> g_peak_threshold_db;

extern std::vector<float> g_heatmap;
extern std::mutex g_heat_mtx;
extern std::atomic<int> g_heat_w;
extern std::atomic<int> g_heat_h;

extern std::vector<float> g_heatmap;
extern std::mutex g_heat_mtx;
extern std::atomic<int> g_heat_w;
extern std::atomic<int> g_heat_h;

extern std::vector<float> g_dist_val;
extern std::vector<float> g_velo_val;
extern std::mutex g_distvelo_mtx;
extern std::atomic<int> g_distvelo_count;

extern std::atomic<float> g_frame_fps;
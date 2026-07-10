// shared_state.cpp
#include "shared_state.h"

std::vector<float> g_plot;
std::mutex g_plot_mtx;

std::vector<float> g_fft_db;
std::mutex g_fft_mtx;

std::vector<float> g_peak_val_db;
std::vector<float> g_peak_idx_f;
std::vector<float> g_peak_hit_f;
std::mutex g_peak_mtx;

std::atomic<int> g_r_max{ 0 };
std::atomic<int> g_pick_r{ 0 };
std::atomic<int> g_plot_len{ 0 };
std::atomic<int> g_plot_len_max{ 0 };
std::atomic<int> g_fft_len{ 0 };
std::atomic<uint64_t> g_frame_id{ 0 };
std::atomic<bool> g_stop_request{ false };

std::atomic<int> g_peak_count{ 0 };
std::atomic<int> g_peak_min_index{ 50 };
std::atomic<int> g_peak_max_index{ 1000 };
std::atomic<float> g_peak_threshold_db{ 95.0f };

std::vector<float> g_heatmap;
std::mutex g_heat_mtx;
std::atomic<int> g_heat_w{ 0 };
std::atomic<int> g_heat_h{ 0 };

std::vector<float> g_dist_val;
std::vector<float> g_velo_val;
std::mutex g_distvelo_mtx;
std::atomic<int> g_distvelo_count{ 0 };

std::atomic<float> g_frame_fps{ 0.0f };
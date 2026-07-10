// acquire.cpp
#include <cstdio>
#include <vector>
#include <deque>
#include <algorithm>
#include <mutex>
#include <limits>
#include <cmath>
#include <iostream>
#include <fstream>
#include <chrono>
#include <cstdint>

#include "udp_sender.h"
#include "inipp.h"
#include "AlazarApi.h"
#include "acquire.h"
#include "shared_state.h"
#include "logger.h"
#include "gpu_stream_fft.h"

#define BUFFER_COUNT 8

static U16* BufferArray[BUFFER_COUNT] = { NULL };

static uint64_t GetTimestampNs()
{
    using namespace std::chrono;
    return (uint64_t)duration_cast<nanoseconds>(
        system_clock::now().time_since_epoch()
    ).count();
}


BOOL LoadConfig(const std::string& path, Config& set)
{
    std::ifstream is(path);
    if (!is) return false;

    inipp::Ini<char> ini;
    ini.parse(is);

    if (!inipp::get_value(ini.sections["Digitizer"], "sample_rate", set.sample_rate)) return false;
    if (!inipp::get_value(ini.sections["Digitizer"], "sample_point", set.sample_point)) return false;
    if (!inipp::get_value(ini.sections["Digitizer"], "A_scanNum", set.A_scanNum)) return false;
    if (!inipp::get_value(ini.sections["Digitizer"], "B_scannum", set.B_scannum)) return false;

    if (!inipp::get_value(ini.sections["Scan_angle"], "x_start_angle", set.x_start_angle)) return false;
    if (!inipp::get_value(ini.sections["Scan_angle"], "x_end_angle", set.x_end_angle)) return false;
    if (!inipp::get_value(ini.sections["Scan_angle"], "y_start_angle", set.y_start_angle)) return false;
    if (!inipp::get_value(ini.sections["Scan_angle"], "y_end_angle", set.y_end_angle)) return false;
    if (!inipp::get_value(ini.sections["Scan_angle"], "Direction", set.direction)) return false;

    if (!inipp::get_value(ini.sections["Laser"], "Bandwidth", set.bandwidth)) return false;
    if (!inipp::get_value(ini.sections["Laser"], "Sweeprate", set.sweeprate)) return false;
    if (!inipp::get_value(ini.sections["Laser"], "Wavelength(nm)", set.wavelength)) return false;

    if (!inipp::get_value(ini.sections["UDP"], "ip", set.udp_ip)) return false;
    if (!inipp::get_value(ini.sections["UDP"], "port", set.udp_port)) return false;

    return true;
}

BOOL ConfigureBoard(HANDLE boardHandle)
{
    RETURN_CODE retCode;

    retCode = AlazarSetCaptureClock(boardHandle, INTERNAL_CLOCK, SAMPLE_RATE_1000MSPS, CLOCK_EDGE_RISING, 0);
    if (retCode != ApiSuccess) { AddLog("AlazarSetCaptureClock failed: %s", AlazarErrorToText(retCode)); return FALSE; }

    retCode = AlazarInputControlEx(boardHandle, CHANNEL_A, DC_COUPLING, INPUT_RANGE_PM_400_MV, IMPEDANCE_50_OHM);
    if (retCode != ApiSuccess) { AddLog("AlazarInputControlEx A failed: %s", AlazarErrorToText(retCode)); return FALSE; }

    retCode = AlazarInputControlEx(boardHandle, CHANNEL_B, DC_COUPLING, INPUT_RANGE_PM_400_MV, IMPEDANCE_50_OHM);
    if (retCode != ApiSuccess) { AddLog("AlazarInputControlEx B failed: %s", AlazarErrorToText(retCode)); return FALSE; }

    retCode = AlazarSetTriggerOperation(boardHandle,
        TRIG_ENGINE_OP_J, TRIG_ENGINE_J, TRIG_EXTERNAL, TRIGGER_SLOPE_POSITIVE, 150,
        TRIG_ENGINE_K, TRIG_DISABLE, TRIGGER_SLOPE_POSITIVE, 128);
    if (retCode != ApiSuccess) { AddLog("AlazarSetTriggerOperation failed: %s", AlazarErrorToText(retCode)); return FALSE; }

    retCode = AlazarSetExternalTrigger(boardHandle, DC_COUPLING, ETR_TTL);
    if (retCode != ApiSuccess) { AddLog("AlazarSetExternalTrigger failed: %s", AlazarErrorToText(retCode)); return FALSE; }

    U32 triggerDelay_samples = 400;
    retCode = AlazarSetTriggerDelay(boardHandle, triggerDelay_samples);
    if (retCode != ApiSuccess) { AddLog("AlazarSetTriggerDelay failed: %s", AlazarErrorToText(retCode)); return FALSE; }

    retCode = AlazarSetTriggerTimeOut(boardHandle, 0);
    if (retCode != ApiSuccess) { AddLog("AlazarSetTriggerTimeOut failed: %s", AlazarErrorToText(retCode)); return FALSE; }

    retCode = AlazarConfigureAuxIO(boardHandle, AUX_IN_TRIGGER_ENABLE, 1);
    if (retCode != ApiSuccess) { AddLog("AlazarConfigureAuxIO failed: %s", AlazarErrorToText(retCode)); return FALSE; }

    AddLog("ConfigureBoard OK");
    return TRUE;
}

BOOL AcquireData(HANDLE boardHandle)
{
    Config set{};
    if (!LoadConfig("Config.ini", set))
    {
        AddLog("Config.ini load failed");
        return FALSE;
    }

    /*
    if ((set.A_scanNum % 2) != 0) {
        AddLog("A_scanNum must be even for up/down pairing. A_scanNum=%d", set.A_scanNum);
        return FALSE;
    }

    if (set.B_scannum <= 0) {
        AddLog("B_scannum invalid: %d", set.B_scannum);
        return FALSE;
    }
    */

    // ---------------------------
    // user parameters
    // ---------------------------
    U32 preTriggerSamples = 0;
    U32 postTriggerSamples = (U32)set.sample_point;
    U32 recordsPerBuffer = (U32)set.A_scanNum;
    U32 channelMask = CHANNEL_A;

    // ---------------------------
    // Angle calculation / parameters
    // ---------------------------
    const int x_pixel_num = set.A_scanNum / 2;
    std::vector<float> x_angle(x_pixel_num);
    std::vector<float> y_angle(set.B_scannum);

    const float DEG2RAD = 3.14159265358979323846f / 180.0f;

    float x_seg = 0.0f;
    if (x_pixel_num > 1) {
        x_seg = (set.x_end_angle - set.x_start_angle) / float(x_pixel_num - 1);
    }

    float y_seg = 0.0f;
    if (set.B_scannum > 1) {
        y_seg = (set.y_end_angle - set.y_start_angle) / float(set.B_scannum - 1);
    }

    for (int x_num = 0; x_num < x_pixel_num; ++x_num) {
        x_angle[x_num] = (90.0f - (set.x_start_angle + x_seg * x_num)) * DEG2RAD;
    }

    for (int y_num = 0; y_num < set.B_scannum; ++y_num) {
        y_angle[y_num] = ((set.y_start_angle + y_seg * y_num) + 90.0f) * DEG2RAD;
    }

    float dis_para = 300000000.0f / (200000.0f * 4.0f * set.bandwidth) * set.sample_rate / set.sample_point;

    // wavelength가 nm 라고 가정
    float wavelength_m = set.wavelength * 1e-9f;
    float velo_para = wavelength_m / 2.0f * set.sample_rate / set.sample_point;

    // ---------------------------
    // derived
    // ---------------------------
    int channelCount = 0;
    for (int ch = 0; ch < 2; ++ch) {
        U32 id = 1U << ch;
        if (channelMask & id) channelCount++;
    }

    U8 bitsPerSample = 0;
    U32 maxSamplesPerChannel = 0;
    RETURN_CODE retCode = AlazarGetChannelInfo(boardHandle, &maxSamplesPerChannel, &bitsPerSample);
    if (retCode != ApiSuccess) {
        AddLog("AlazarGetChannelInfo failed: %s", AlazarErrorToText(retCode));
        return FALSE;
    }

    float bytesPerSample = float((bitsPerSample + 7) / 8);
    U32 samplesPerRecord = preTriggerSamples + postTriggerSamples;
    U32 bytesPerRecord = U32(bytesPerSample * samplesPerRecord + 0.5f);
    U32 bytesPerBuffer = bytesPerRecord * recordsPerBuffer * channelCount;

    const int N = int(samplesPerRecord);
    const int BATCH = int(recordsPerBuffer) * channelCount;
    const int K = (N / 2 + 1);
    const int pair_count = BATCH / 2;

    // ---------------------------
    // shared state init
    // ---------------------------
    g_r_max.store(int(recordsPerBuffer) - 1, std::memory_order_release);
    g_plot_len_max.store(int(samplesPerRecord), std::memory_order_release);
    g_fft_len.store(K, std::memory_order_release);
    g_peak_count.store(BATCH, std::memory_order_release);
    g_peak_min_index.store(42, std::memory_order_release);
    g_peak_max_index.store(K - 1, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lk(g_distvelo_mtx);
        g_dist_val.assign(pair_count, 0.0f);
        g_velo_val.assign(pair_count, 0.0f);
    }
    g_distvelo_count.store(pair_count, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lk(g_plot_mtx);
        g_plot.assign(samplesPerRecord, 0.0f);
    }
    {
        std::lock_guard<std::mutex> lk(g_fft_mtx);
        g_fft_db.assign(K, 0.0f);
    }
    {
        std::lock_guard<std::mutex> lk(g_peak_mtx);
        g_peak_val_db.assign(BATCH, std::numeric_limits<float>::quiet_NaN());
        g_peak_idx_f.assign(BATCH, std::numeric_limits<float>::quiet_NaN());
        g_peak_hit_f.assign(BATCH, 0.0f);
    }

    // heatmap buffer (z value)
    {
        std::lock_guard<std::mutex> lk(g_heat_mtx);
        g_heatmap.assign((size_t)pair_count * (size_t)set.B_scannum, 0.0f);
    }
    g_heat_w.store(pair_count, std::memory_order_release);
    g_heat_h.store(set.B_scannum, std::memory_order_release);

    g_frame_id.store(0, std::memory_order_release);
    g_frame_fps.store(0.0f, std::memory_order_release);

    // ---------------------------
    // runtime resources
    // ---------------------------
    bool success = true;
    StreamCtx* gpu = nullptr;

    UdpSender udp{};
    bool udp_enabled = false;

    struct PendingUdpFrame {
        uint32_t frame_seq = 0;
        bool active = false;
        int ready_count = 0;
        uint64_t timestamp_ns = 0;
        std::vector<uint8_t> line_ready;
        std::vector<PointXYZIV> points;
    };

    PendingUdpFrame udp_frames[2];
    const uint32_t udp_points_per_packet = 400; // 20(header)+60*20=1220 bytes

    if (!set.udp_ip.empty() && set.udp_port > 0) {
        udp_enabled = UdpSenderInit(udp, set.udp_ip, set.udp_port);
        if (udp_enabled) {
            AddLog("UDP sender ready: %s:%d", set.udp_ip.c_str(), set.udp_port);
            for (int i = 0; i < 2; ++i) {
                udp_frames[i].line_ready.assign((size_t)set.B_scannum, 0);
                udp_frames[i].points.assign((size_t)pair_count * (size_t)set.B_scannum, PointXYZIV{});
            }
        }
        else {
            AddLog("UDP sender init failed: %s:%d", set.udp_ip.c_str(), set.udp_port);
        }
    }

    auto cleanup_all = [&]() {
        AlazarAbortAsyncRead(boardHandle);

        if (gpu) {
            CleanupStreamFFT(gpu);
            gpu = nullptr;
        }

        if (udp_enabled) {
            UdpSenderClose(udp);
            udp_enabled = false;
        }

        for (U32 i = 0; i < BUFFER_COUNT; ++i) {
            if (BufferArray[i]) {
                AlazarFreeBufferU16(boardHandle, BufferArray[i]);
                BufferArray[i] = NULL;
            }
        }
        };

    // ---------------------------
    // allocate DMA buffers
    // ---------------------------
    for (U32 i = 0; i < BUFFER_COUNT; ++i) {
        BufferArray[i] = (U16*)AlazarAllocBufferU16(boardHandle, bytesPerBuffer);
        if (!BufferArray[i]) {
            AddLog("AlazarAllocBufferU16 failed (bytes=%u)", bytesPerBuffer);
            cleanup_all();
            return FALSE;
        }
    }

    // ---------------------------
    // record size
    // ---------------------------
    retCode = AlazarSetRecordSize(boardHandle, preTriggerSamples, postTriggerSamples);
    if (retCode != ApiSuccess) {
        AddLog("AlazarSetRecordSize failed: %s", AlazarErrorToText(retCode));
        cleanup_all();
        return FALSE;
    }

    // ---------------------------
    // async read config
    // ---------------------------
    {
        U32 recordsPerAcquisition = 0x7FFFFFFF;
        U32 admaFlags = ADMA_EXTERNAL_STARTCAPTURE | ADMA_NPT | ADMA_FIFO_ONLY_STREAMING;

        retCode = AlazarBeforeAsyncRead(
            boardHandle,
            channelMask,
            -(long)preTriggerSamples,
            samplesPerRecord,
            recordsPerBuffer,
            recordsPerAcquisition,
            admaFlags);

        if (retCode != ApiSuccess) {
            AddLog("AlazarBeforeAsyncRead failed: %s", AlazarErrorToText(retCode));
            cleanup_all();
            return FALSE;
        }
    }

    // ---------------------------
    // GPU init
    // ---------------------------
    {
        int gr = InitStreamFFT(
            N,
            BATCH,
            dis_para,
            velo_para,
            set.B_scannum,
            x_angle.data(),
            y_angle.data(),
            &gpu);

        if (gr != 0 || !gpu) {
            AddLog("InitStreamFFT failed: %d", gr);
            cleanup_all();
            return FALSE;
        }
    }

    // slot별 host output 버퍼
    std::vector<float> h_fft0(K), h_fft1(K);
    float* h_fft_slot[2] = { h_fft0.data(), h_fft1.data() };

    std::vector<PeakResult> h_peak0(BATCH), h_peak1(BATCH);
    PeakResult* h_peak_slot[2] = { h_peak0.data(), h_peak1.data() };

    std::vector<Dis_value> h_dis0(pair_count), h_dis1(pair_count);
    Dis_value* dis_value_slot[2] = { h_dis0.data(), h_dis1.data() };

    // ---------------------------
    // post buffers
    // ---------------------------
    std::deque<U32> postedQ;
    for (U32 i = 0; i < BUFFER_COUNT; ++i) {
        retCode = AlazarPostAsyncBuffer(boardHandle, BufferArray[i], bytesPerBuffer);
        if (retCode != ApiSuccess) {
            AddLog("AlazarPostAsyncBuffer failed: %s", AlazarErrorToText(retCode));
            cleanup_all();
            return FALSE;
        }
        postedQ.push_back(i);
    }

    retCode = AlazarStartCapture(boardHandle);
    if (retCode != ApiSuccess) {
        AddLog("AlazarStartCapture failed: %s", AlazarErrorToText(retCode));
        cleanup_all();
        return FALSE;
    }

    AddLog("Acquire START (N=%u, recordsPerBuffer=%u, bytesPerBuffer=%u)",
        samplesPerRecord, recordsPerBuffer, bytesPerBuffer);

    struct SlotHold {
        U16* ptr;
        U32 idx;
        bool repost_pending;
        int line_idx;
        uint32_t frame_seq;
    };

    SlotHold inFlight[2] = {
        { nullptr, 0, false, -1, 0 },
        { nullptr, 0, false, -1, 0 }
    };

    uint64_t recvCount = 0;
    uint64_t enqCount = 0;
    uint64_t dropCount = 0;

    uint64_t last_frame_timestamp_ns = 0;
    float fps_ema = 0.0f;
    bool fps_initialized = false;

    int next_line_idx = 0;
    uint32_t next_frame_seq = 0;

    while (success && !g_stop_request.load(std::memory_order_relaxed))
    {
        // --------------------------------------------------
        // 1) H2D done -> ATS buffer repost
        // --------------------------------------------------
        for (;;) {
            int s = TryGetH2DDone(gpu);
            if (s < 0) break;

            if (s < 2 && inFlight[s].ptr && inFlight[s].repost_pending) {
                retCode = AlazarPostAsyncBuffer(boardHandle, inFlight[s].ptr, bytesPerBuffer);
                if (retCode != ApiSuccess) {
                    AddLog("AlazarPostAsyncBuffer failed(after H2D done): %s", AlazarErrorToText(retCode));
                    success = false;
                    break;
                }
                postedQ.push_back(inFlight[s].idx);
                inFlight[s].repost_pending = false;
            }
        }
        if (!success) break;

        // --------------------------------------------------
        // 2) full GPU done -> FFT + peak + heatmap 반영 + UDP frame accumulation
        // --------------------------------------------------
        for (;;) {
            int s = TryDequeue(gpu);
            if (s < 0) break;

            const int line_idx = inFlight[s].line_idx;
            const uint32_t frame_seq = inFlight[s].frame_seq;

            if (line_idx < 0 || line_idx >= set.B_scannum) {
                AddLog("Invalid line_idx from slot %d: %d", s, line_idx);
                success = false;
                break;
            }

            // FFT
            {
                std::lock_guard<std::mutex> lk(g_fft_mtx);
                if ((int)g_fft_db.size() != K) g_fft_db.resize(K);
                std::copy(h_fft_slot[s], h_fft_slot[s] + K, g_fft_db.begin());
            }

            // Peak per batch
            {
                std::lock_guard<std::mutex> lk(g_peak_mtx);

                if ((int)g_peak_val_db.size() != BATCH) g_peak_val_db.resize(BATCH);
                if ((int)g_peak_idx_f.size() != BATCH) g_peak_idx_f.resize(BATCH);
                if ((int)g_peak_hit_f.size() != BATCH) g_peak_hit_f.resize(BATCH);

                for (int i = 0; i < BATCH; ++i) {
                    if (h_peak_slot[s][i].hit) {
                        g_peak_val_db[i] = h_peak_slot[s][i].max_val_db;
                        g_peak_idx_f[i] = (float)h_peak_slot[s][i].max_idx;
                        g_peak_hit_f[i] = 1.0f;
                    }
                    else {
                        g_peak_val_db[i] = std::numeric_limits<float>::quiet_NaN();
                        g_peak_idx_f[i] = std::numeric_limits<float>::quiet_NaN();
                        g_peak_hit_f[i] = 0.0f;
                    }
                }
            }

            // Distance / Velocity per pair
            {
                std::lock_guard<std::mutex> lk(g_distvelo_mtx);

                if ((int)g_dist_val.size() != pair_count) g_dist_val.resize(pair_count);
                if ((int)g_velo_val.size() != pair_count) g_velo_val.resize(pair_count);

                for (int i = 0; i < pair_count; ++i) {
                    int x_store;
                    if (set.direction) {
                        x_store = ((line_idx & 1) == 0) ? i : (pair_count - 1 - i);
                    }
                    else {
                        x_store = ((line_idx & 1) == 0) ? (pair_count - 1 - i) : i;
                    }

                    g_dist_val[x_store] = dis_value_slot[s][i].y;
                    g_velo_val[x_store] = dis_value_slot[s][i].velo;
                }
            }

            // Z heatmap update
            {
                std::lock_guard<std::mutex> lk(g_heat_mtx);

                for (int x = 0; x < pair_count; ++x) {
                    float z = dis_value_slot[s][x].y;
                    if (std::isnan(z))
                        z = 0.0f;

                    int x_store;
                    if (set.direction) {
                        x_store = ((line_idx & 1) == 0) ? x : (pair_count - 1 - x);
                    }
                    else {
                        x_store = ((line_idx & 1) == 0) ? (pair_count - 1 - x) : x;
                    }

                    g_heatmap[(size_t)line_idx * (size_t)pair_count + (size_t)x_store] = z;
                }
            }

            // UDP frame accumulation
            if (udp_enabled) {
                PendingUdpFrame& pf = udp_frames[frame_seq & 1U];

                if (!pf.active || pf.frame_seq != frame_seq) {
                    pf.active = true;
                    pf.frame_seq = frame_seq;
                    pf.ready_count = 0;
                    pf.timestamp_ns = 0;
                    std::fill(pf.line_ready.begin(), pf.line_ready.end(), 0);
                    std::fill(pf.points.begin(), pf.points.end(), PointXYZIV{});
                }

                for (int x = 0; x < pair_count; ++x) {
                    const Dis_value& dv = dis_value_slot[s][x];

                    int x_store;
                    if (set.direction) {
                        x_store = ((line_idx & 1) == 0) ? x : (pair_count - 1 - x);
                    }
                    else {
                        x_store = ((line_idx & 1) == 0) ? (pair_count - 1 - x) : x;
                    }

                    size_t idx_pt = (size_t)line_idx * (size_t)pair_count + (size_t)x_store;

                    pf.points[idx_pt].x = std::isnan(dv.x) ? 0.0f : dv.x;
                    pf.points[idx_pt].y = std::isnan(dv.y) ? 0.0f : dv.y;
                    pf.points[idx_pt].z = std::isnan(dv.z) ? 0.0f : dv.z;
                    pf.points[idx_pt].inten = std::isnan(dv.inten) ? 0.0f : dv.inten;
                    pf.points[idx_pt].velo = std::isnan(dv.velo) ? 0.0f : dv.velo;
                }

                if (!pf.line_ready[(size_t)line_idx]) {
                    if (pf.ready_count == 0) {
                        pf.timestamp_ns = GetTimestampNs();
                    }

                    pf.line_ready[(size_t)line_idx] = 1;
                    ++pf.ready_count;
                }

                if (pf.ready_count == set.B_scannum) {
                    float fps_inst = 0.0f;
                    float fps_show = 0.0f;
                    bool fps_valid = false;

                    if (last_frame_timestamp_ns != 0 && pf.timestamp_ns > last_frame_timestamp_ns) {
                        double dt_sec = double(pf.timestamp_ns - last_frame_timestamp_ns) * 1e-9;
                        if (dt_sec > 0.0) {
                            fps_inst = (float)(1.0 / dt_sec);

                            if (!fps_initialized) {
                                fps_ema = fps_inst;
                                fps_initialized = true;
                            }
                            else {
                                const float alpha = 0.2f;
                                fps_ema = alpha * fps_inst + (1.0f - alpha) * fps_ema;
                            }

                            fps_show = fps_ema;
                            fps_valid = true;

                            // 여기 추가
                            g_frame_fps.store(fps_show, std::memory_order_relaxed);
                        }
                    }

                    bool ok = UdpSendFrameXYZIV(
                        udp,
                        pf.frame_seq,
                        pf.timestamp_ns,
                        pf.points.data(),
                        (uint32_t)pf.points.size(),
                        udp_points_per_packet);

                    if (!ok) {
                        AddLog("UDP frame send failed: frame=%u", pf.frame_seq);
                    }
                    else {
                        if (fps_valid) {
                            AddLog("UDP frame sent: frame=%u, points=%u, ts=%llu, fps=%.2f",
                                pf.frame_seq,
                                (unsigned)pf.points.size(),
                                (unsigned long long)pf.timestamp_ns,
                                fps_show);
                        }
                        else {
                            AddLog("UDP frame sent: frame=%u, points=%u, ts=%llu, fps=---",
                                pf.frame_seq,
                                (unsigned)pf.points.size(),
                                (unsigned long long)pf.timestamp_ns);
                        }
                    }

                    last_frame_timestamp_ns = pf.timestamp_ns;

                    pf.active = false;
                    pf.ready_count = 0;
                    std::fill(pf.line_ready.begin(), pf.line_ready.end(), 0);
                }
            }

            g_frame_id.fetch_add(1, std::memory_order_release);
            inFlight[s] = { nullptr, 0, false, -1, 0 };
        }
        if (!success) break;

        // --------------------------------------------------
        // 3) postedQ 에서 wait 대상 하나 꺼내기
        // --------------------------------------------------
        if (postedQ.empty()) {
            AddLog("ERROR: postedQ empty. No posted DMA buffers left.");
            success = false;
            break;
        }

        U32 idx = postedQ.front();
        postedQ.pop_front();

        U16* pBuffer = BufferArray[idx];
        U32 timeout_ms = 5000;

        retCode = AlazarWaitAsyncBufferComplete(boardHandle, pBuffer, timeout_ms);
        if (retCode != ApiSuccess) {
            AddLog("WaitAsyncBufferComplete failed: %s", AlazarErrorToText(retCode));
            success = false;
            break;
        }
        ++recvCount;

        // --------------------------------------------------
        // 4) r번째 raw signal plot 갱신
        // --------------------------------------------------
        int r_plot = g_pick_r.load(std::memory_order_relaxed);
        r_plot = std::clamp(r_plot, 0, (int)recordsPerBuffer - 1);

        int want_len = (int)samplesPerRecord;
        U32 off = (U32)r_plot * samplesPerRecord; // CHANNEL_A only

        {
            std::lock_guard<std::mutex> lk(g_plot_mtx);
            if ((int)g_plot.size() != want_len) g_plot.resize(want_len);
            for (int i = 0; i < want_len; ++i) {
                int sv = (int)pBuffer[off + (U32)i] - 32768;
                g_plot[i] = (float)sv / 32768.0f;
            }
        }

        // --------------------------------------------------
        // 5) free slot 찾기
        // --------------------------------------------------
        int s_free = -1;
        for (int s = 0; s < 2; ++s) {
            if (inFlight[s].ptr == nullptr) {
                s_free = s;
                break;
            }
        }

        if (s_free >= 0) {
            const int line_idx_to_enqueue = next_line_idx;
            const uint32_t frame_seq_to_enqueue = next_frame_seq;

            int r_fft = g_pick_r.load(std::memory_order_relaxed);
            r_fft = std::clamp(r_fft, 0, (int)recordsPerBuffer - 1);

            int min_index = g_peak_min_index.load(std::memory_order_relaxed);
            min_index = std::clamp(min_index, 0, K - 1);

            int max_index = g_peak_max_index.load(std::memory_order_relaxed);
            max_index = std::clamp(max_index, 0, K - 1);

            if (max_index < min_index) {
                std::swap(min_index, max_index);
            }

            float threshold_db = g_peak_threshold_db.load(std::memory_order_relaxed);

            int gr = EnqueueWholeBuffer_GetR(
                gpu,
                s_free,
                (const uint16_t*)pBuffer,
                r_fft,
                h_fft_slot[s_free],
                min_index,
                max_index,
                threshold_db,
                h_peak_slot[s_free],
                line_idx_to_enqueue,
                set.direction,
                dis_value_slot[s_free]
            );

            if (gr != 0) {
                AddLog("GPU Enqueue failed: %d", gr);
                success = false;
                break;
            }

            inFlight[s_free] = { pBuffer, idx, true, line_idx_to_enqueue, frame_seq_to_enqueue };
            ++enqCount;

            ++next_line_idx;
            if (next_line_idx >= set.B_scannum) {
                next_line_idx = 0;
                ++next_frame_seq;
            }
        }
        else {
            ++dropCount;

            retCode = AlazarPostAsyncBuffer(boardHandle, pBuffer, bytesPerBuffer);
            if (retCode != ApiSuccess) {
                AddLog("AlazarPostAsyncBuffer failed(after drop): %s", AlazarErrorToText(retCode));
                success = false;
                break;
            }
            postedQ.push_back(idx);

            if ((dropCount % 100) == 1) {
                AddLog("GPU overflow/drop: recv=%llu enq=%llu drop=%llu",
                    (unsigned long long)recvCount,
                    (unsigned long long)enqCount,
                    (unsigned long long)dropCount);
            }
        }
    }

    AddLog("Acquire STOP (success=%d, recv=%llu, enq=%llu, drop=%llu)",
        success ? 1 : 0,
        (unsigned long long)recvCount,
        (unsigned long long)enqCount,
        (unsigned long long)dropCount);

    cleanup_all();
    return success ? TRUE : FALSE;
}
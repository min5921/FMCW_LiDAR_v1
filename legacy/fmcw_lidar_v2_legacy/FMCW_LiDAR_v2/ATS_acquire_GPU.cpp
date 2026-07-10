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

#include "inipp.h"
#include "AlazarApi.h"
#include "acquire.h"
#include "shared_state.h"
#include "logger.h"
#include "gpu_stream_fft.h"

#define BUFFER_COUNT 8

static U16* BufferArray[BUFFER_COUNT] = { NULL };

BOOL LoadConfig(const std::string& path, Config& cfg)
{
    std::ifstream is(path);
    if (!is) return false;

    inipp::Ini<char> ini;
    ini.parse(is);

    inipp::get_value(ini.sections["Digitizer"], "sample_rate", cfg.sample_rate);
    inipp::get_value(ini.sections["Digitizer"], "sample_point", cfg.sample_point);
    inipp::get_value(ini.sections["Digitizer"], "A_scanNum", cfg.A_scanNum);
    inipp::get_value(ini.sections["Digitizer"], "B_scannum", cfg.B_scannum);

    inipp::get_value(ini.sections["Scan_angle"], "x_start_angle", cfg.x_start_angle);
    inipp::get_value(ini.sections["Scan_angle"], "x_end_angle", cfg.x_end_angle);
    inipp::get_value(ini.sections["Scan_angle"], "y_start_angle", cfg.y_start_angle);
    inipp::get_value(ini.sections["Scan_angle"], "y_end_angle", cfg.y_end_angle);
    inipp::get_value(ini.sections["Scan_angle"], "Direction", cfg.direction);


    inipp::get_value(ini.sections["Laser"], "Bandwidth", cfg.bandwidth);
    inipp::get_value(ini.sections["Laser"], "Sweeprate", cfg.sweeprate);
    inipp::get_value(ini.sections["Laser"], "Wavelength(nm)", cfg.wavelength);


    inipp::get_value(ini.sections["UDP"], "ip", cfg.udp_ip);
    inipp::get_value(ini.sections["UDP"], "port", cfg.udp_port);

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

    retCode = AlazarConfigureAuxIO(boardHandle, AUX_IN_TRIGGER_ENABLE, 1);//AUX_OUT_TRIGGER, 0);
    if (retCode != ApiSuccess) { AddLog("AlazarConfigureAuxIO failed: %s", AlazarErrorToText(retCode)); return FALSE; }

    AddLog("ConfigureBoard OK");
    return TRUE;
}

BOOL AcquireData(HANDLE boardHandle)
{

    // Load Config.ini
    // 
    Config cfg;
    
    if (!LoadConfig("Config.ini", cfg))
    {
        AddLog("config load failed\n");
        //return -1;
    }
    
    // ---------------------------
    // user parameters
    // ---------------------------
    U32 preTriggerSamples = 0;
    U32 postTriggerSamples = 2048;//cfg.sample_point;
    U32 recordsPerBuffer = 1900;//cfg.A_scanNum;
    U32 channelMask = CHANNEL_A;

    // ---------------------------
    // Angle_calculation && dis_velo_para cal
    // ---------------------------
    std::vector<float> x_angle(cfg.A_scanNum);
    std::vector<float> y_angle(cfg.B_scannum);
    float x_seg = (cfg.x_start_angle - cfg.x_end_angle) / (cfg.A_scanNum - 1);
    float y_seg = (cfg.y_start_angle - cfg.y_end_angle) / (cfg.B_scannum - 1);

    for (int x_num = 0; x_num < cfg.A_scanNum/2; ++x_num) {
        x_angle[x_num] = cfg.x_start_angle + x_seg * x_num;
    }

    for (int y_num = 0; y_num < cfg.B_scannum; ++y_num) {
        y_angle[y_num] = cfg.y_start_angle + y_seg * y_num;
    }

    float dis_para = 300000000.0f / (200000.0f * 4.0f * cfg.bandwidth);
    float velo_para = cfg.wavelength / 2.0f;


    // ---------------------------
    // derived
    // ---------------------------
    int channelCount = 0;
    for (int ch = 0; ch < 2; ++ch) {
        U32 id = 1U << ch;
        if (channelMask & id) channelCount++;
    }

    U8  bitsPerSample = 0;
    U32 maxSamplesPerChannel = 0;
    RETURN_CODE retCode = AlazarGetChannelInfo(boardHandle, &maxSamplesPerChannel, &bitsPerSample);
    if (retCode != ApiSuccess) {
        AddLog("AlazarGetChannelInfo failed: %s", AlazarErrorToText(retCode));
        return FALSE;
    }

    float bytesPerSample = (float)((bitsPerSample + 7) / 8);
    U32 samplesPerRecord = preTriggerSamples + postTriggerSamples;
    U32 bytesPerRecord = (U32)(bytesPerSample * samplesPerRecord + 0.5f);
    U32 bytesPerBuffer = bytesPerRecord * recordsPerBuffer * channelCount;

    const int N = (int)samplesPerRecord;
    const int BATCH = (int)recordsPerBuffer * channelCount;
    const int K = (N / 2 + 1);

    // ---------------------------
    // shared state init
    // ---------------------------
    g_r_max.store((int)recordsPerBuffer - 1, std::memory_order_release);
    g_plot_len_max.store((int)samplesPerRecord, std::memory_order_release);
    g_fft_len.store(K, std::memory_order_release);
    g_peak_count.store(BATCH, std::memory_order_release);

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

    g_frame_id.store(0, std::memory_order_release);

    // ---------------------------
    // runtime resources
    // ---------------------------
    bool success = true;
    StreamCtx* gpu = nullptr;

    auto cleanup_all = [&]() {
        AlazarAbortAsyncRead(boardHandle);

        if (gpu) {
            CleanupStreamFFT(gpu);
            gpu = nullptr;
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
        int gr = InitStreamFFT(N, BATCH, dis_para, velo_para, cfg.B_scannum, x_angle.data(), y_angle.data(), &gpu);
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

    std::vector<Dis_value> h_dis0(BATCH), h_dis1(BATCH);
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
        U32  idx;
        bool repost_pending;
    };

    SlotHold inFlight[2] = {
        { nullptr, 0, false },
        { nullptr, 0, false }
    };

    uint64_t recvCount = 0;
    uint64_t enqCount = 0;
    uint64_t dropCount = 0;
    int current_B_scanum = 0;

    while (success && !g_stop_request.load(std::memory_order_relaxed))
    {
        // Bscan 초기화
        if (current_B_scanum >= cfg.B_scannum) { current_B_scanum = 0; }
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
        // 2) full GPU done -> FFT + peak 결과 공유 + slot free
        // --------------------------------------------------
        for (;;) {
            int s = TryDequeue(gpu);
            if (s < 0) break;

            // FFT (선택된 r record의 spectrum)
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

            g_frame_id.fetch_add(1, std::memory_order_release);
            inFlight[s] = { nullptr, 0, false };
        }

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
        U32 timeout_ms = 5;

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
            int r_fft = g_pick_r.load(std::memory_order_relaxed);
            r_fft = std::clamp(r_fft, 0, (int)recordsPerBuffer - 1);

            int min_index = g_peak_min_index.load(std::memory_order_relaxed);
            min_index = std::clamp(min_index, 0, K - 1);

            float threshold_db = g_peak_threshold_db.load(std::memory_order_relaxed);

            int gr = EnqueueWholeBuffer_GetR(
                gpu,
                s_free,
                (const uint16_t*)pBuffer,
                r_fft,
                h_fft_slot[s_free],
                min_index,
                threshold_db,
                h_peak_slot[s_free],
                current_B_scanum,
                cfg.direction,
                dis_value_slot[s_free]

            );

            if (gr != 0) {
                AddLog("GPU Enqueue failed: %d", gr);
                success = false;
                break;
            }

            inFlight[s_free] = { pBuffer, idx, true };
            ++enqCount;
            ++current_B_scanum;

        }
        else {
            // slot full -> drop
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
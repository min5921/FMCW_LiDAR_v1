//gpu_stream_fft.h
#pragma once
#include <cstdint>

struct StreamCtx;
struct Dis_value {
    float x = 0;
    float y = 0;
    float z = 0;
    float inten = 0;
    float velo = 0;
};
struct PeakResult {
    float max_val_db;
    int   max_idx;
    int   hit;
};

// 초기화
int InitStreamFFT(int N, int BATCH,float dis_para, float velo_para,int B_scannum, float* h_x_scan_angle, float* h_y_scan_angle, StreamCtx** out);

// slot에 whole buffer enqueue
// h_u16_whole : 길이 N*BATCH 의 host input
// r            : D2H로 가져올 spectrum index [0, BATCH-1]
// h_fft_db_K   : 길이 K(=N/2+1) 의 host output
int EnqueueWholeBuffer_GetR(
    StreamCtx* ctx,
    int slot,
    const uint16_t* h_u16_whole,
    int r,
    float* h_fft_db_K,
    int min_index,
    int max_index,
    float threshold_db,
    PeakResult* h_peak_out,
    int current_Bscannum,
    bool direction,
    Dis_value* h_dis_out);

// H2D 완료된 slot 하나 반환, 없으면 -1
int TryGetH2DDone(StreamCtx* ctx);

// 전체 작업(D2H 포함) 완료된 slot 하나 반환, 없으면 -1
int TryDequeue(StreamCtx* ctx);

// 정리
void CleanupStreamFFT(StreamCtx* ctx);
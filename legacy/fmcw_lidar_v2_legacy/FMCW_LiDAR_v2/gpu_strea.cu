//gpu_strea.cu
#include "gpu_stream_fft.h"

#include <cuda_runtime.h>
#include <cufft.h>

#include <cstdio>
#include <cmath>
#include <algorithm>



// ---------- Streaming structs ----------
struct Slot {
    cudaStream_t  stream = nullptr;
    cudaEvent_t   h2d_done = nullptr;   // H2D 완료
    cudaEvent_t   done = nullptr;       // 전체 완료
    cufftHandle   plan = 0;

    uint16_t* d_u16 = nullptr;      // input whole buffer
    float* d_in = nullptr;       // float input
    cufftComplex* d_out = nullptr;      // FFT output
    float* d_mag = nullptr;      // magnitude(dB)
    PeakResult* d_peak = nullptr;     // peak result [BATCH]
    Dis_value* dis_velo = nullptr;     // xyz_value [BATCH]

    bool          in_flight = false;
    bool          h2d_reported = false;
};



struct StreamCtx {
    int N = 0;
    int BATCH = 0;
    int x_pixel_num = 0;
    int B_scannum = 0;
    int K = 0;
    float dis_para = 0;
    float velo_para = 0;
    float* d_x_scan_angle = nullptr;
    float* d_y_scan_angle = nullptr;

    size_t inCount = 0;
    size_t outCount = 0;

    float* d_win = nullptr;

    static constexpr int NSLOT = 2;
    Slot slot[NSLOT];
};




// ---------- Error helpers ----------
static inline int cuda_to_code(cudaError_t e) {
    return (e == cudaSuccess) ? 0 : (int)e;
}

static inline int cufft_to_code(cufftResult r) {
    return (r == CUFFT_SUCCESS) ? 0 : (10000 + (int)r);
}

static inline bool checkCuda(cudaError_t e, int& ret, const char* what) {
    if (e != cudaSuccess) {
        ret = cuda_to_code(e);
        std::fprintf(stderr, "[CUDA] %s failed: %s (%d)\n",
            what, cudaGetErrorString(e), ret);
        return false;
    }
    return true;
}

static inline bool checkCufft(cufftResult r, int& ret, const char* what) {
    if (r != CUFFT_SUCCESS) {
        ret = cufft_to_code(r);
        std::fprintf(stderr, "[CUFFT] %s failed: result=%d (code=%d)\n",
            what, (int)r, ret);
        return false;
    }
    return true;
}

// ---------- Kernels ----------
__global__ void make_hann(float* win, int N) {
    int i = (int)(blockIdx.x * blockDim.x + threadIdx.x);
    if (i < N) {
        float x = 2.0f * 3.14159265358979323846f * (float)i / (float)(N - 1);
        win[i] = 0.5f * (1.0f - cosf(x));
    }
}

__global__ void u16_to_f32_hann(
    const uint16_t* __restrict__ in_u16,
    float* __restrict__ out_f32,
    const float* __restrict__ win,
    size_t total,
    int N)
{
    size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < total) {
        int n = (int)(i % (size_t)N);
        out_f32[i] = (float)in_u16[i] * win[n];
    }
}

__global__ void dis_velo_calculation(
    const PeakResult* __restrict__ d_peak,
    Dis_value* __restrict__ dis_velo,
    const float* __restrict__ d_x_scan_angle,
    const float* __restrict__ d_y_scan_angle,
    int x_pixel_num,
    float dis_para,
    float velo_para,
    bool direction,
    int current_b_scannum,
    int b_scannum)
{
    int tid = (int)(blockIdx.x * blockDim.x + threadIdx.x);
    if (tid >= x_pixel_num) return;
    if (current_b_scannum < 0 || current_b_scannum >= b_scannum) return;

    int i_up = tid * 2;
    int i_down = i_up + 1;

    PeakResult pk_up = d_peak[i_up];
    PeakResult pk_down = d_peak[i_down];

    if (!pk_up.hit || !pk_down.hit || pk_up.max_idx < 0 || pk_down.max_idx < 0) {
        dis_velo[tid].x = NAN;
        dis_velo[tid].y = NAN;
        dis_velo[tid].z = NAN;
        dis_velo[tid].inten = NAN;
        dis_velo[tid].velo = NAN;
        return;
    }

    int x_idx;
    if (direction) {
        x_idx = ((current_b_scannum & 1) == 0) ? tid : (x_pixel_num - 1 - tid);
    }
    else {
        x_idx = ((current_b_scannum & 1) == 0) ? (x_pixel_num - 1 - tid) : tid;
    }

    float x_ang = d_x_scan_angle[x_idx];
    float y_ang = d_y_scan_angle[current_b_scannum];

    float dis = ((float)pk_up.max_idx + (float)pk_down.max_idx) * 0.5f * dis_para;
    float velo = ((float)pk_up.max_idx - (float)pk_down.max_idx) * 0.5f * velo_para;

    dis_velo[tid].x = cosf(x_ang) * sinf(y_ang) * dis;
    dis_velo[tid].y = sinf(x_ang) * sinf(y_ang) * dis;
    dis_velo[tid].z = cosf(y_ang) * dis;
    dis_velo[tid].inten = 0.5f * (pk_up.max_val_db + pk_down.max_val_db);
    dis_velo[tid].velo = velo;
}

__global__ void complex_to_mag_db(
    const cufftComplex* in,
    float* out,
    size_t n)
{
    size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        float re = in[i].x;
        float im = in[i].y;
        float p = re * re + im * im;
        p = fmaxf(p, 1e-20f);
        out[i] = 10.0f * log10f(p);
    }
}

// ---------- Peak search kernel ----------
// PeakResult 는 gpu_stream_fft.h 에 정의되어 있다고 가정
__global__ void detect_peak_per_record(
    const float* __restrict__ d_mag,
    int K,
    int BATCH,
    int min_index,
    int max_index,
    float threshold_db,
    PeakResult* __restrict__ d_peak)
{
    int r = blockIdx.x;
    if (r >= BATCH) return;

    extern __shared__ unsigned char smem[];
    float* s_val = (float*)smem;
    int* s_idx = (int*)(s_val + blockDim.x);

    float best_val = -1e30f;
    int best_idx = -1;

    const int base = r * K;

    for (int i = min_index + threadIdx.x; i <= max_index; i += blockDim.x) {
        float v = d_mag[base + i];
        if (v >= threshold_db && v > best_val) {
            best_val = v;
            best_idx = i;
        }
    }

    s_val[threadIdx.x] = best_val;
    s_idx[threadIdx.x] = best_idx;
    __syncthreads();

    for (int stride = blockDim.x / 2; stride > 0; stride >>= 1) {
        if (threadIdx.x < stride) {
            if (s_val[threadIdx.x + stride] > s_val[threadIdx.x]) {
                s_val[threadIdx.x] = s_val[threadIdx.x + stride];
                s_idx[threadIdx.x] = s_idx[threadIdx.x + stride];
            }
        }
        __syncthreads();
    }

    if (threadIdx.x == 0) {
        PeakResult out;
        if (s_idx[0] < 0) {
            out.max_val_db = NAN;
            out.max_idx = -1;
            out.hit = 0;
        }
        else {
            out.max_val_db = s_val[0];
            out.max_idx = s_idx[0];
            out.hit = 1;
        }
        d_peak[r] = out;
    }
}



// ---------- Cleanup ----------
static void cleanup(StreamCtx* ctx) {
    if (!ctx) return;

    // stream sync
    for (int s = 0; s < StreamCtx::NSLOT; ++s) {
        if (ctx->slot[s].stream) {
            cudaStreamSynchronize(ctx->slot[s].stream);
        }
    }

    for (int s = 0; s < StreamCtx::NSLOT; ++s) {
        auto& sl = ctx->slot[s];

        if (sl.plan) { cufftDestroy(sl.plan); sl.plan = 0; }
        if (sl.d_peak) { cudaFree(sl.d_peak); sl.d_peak = nullptr; }
        if (sl.dis_velo) { cudaFree(sl.dis_velo); sl.dis_velo = nullptr; }
        if (sl.d_mag) { cudaFree(sl.d_mag); sl.d_mag = nullptr; }
        if (sl.d_out) { cudaFree(sl.d_out); sl.d_out = nullptr; }
        if (sl.d_in) { cudaFree(sl.d_in);  sl.d_in = nullptr; }
        if (sl.d_u16) { cudaFree(sl.d_u16); sl.d_u16 = nullptr; }

        if (sl.h2d_done) { cudaEventDestroy(sl.h2d_done); sl.h2d_done = nullptr; }
        if (sl.done) { cudaEventDestroy(sl.done);     sl.done = nullptr; }

        if (sl.stream) { cudaStreamDestroy(sl.stream); sl.stream = nullptr; }

        sl.in_flight = false;
        sl.h2d_reported = false;
    }
    if (ctx->d_win) {
        cudaFree(ctx->d_win);
        ctx->d_win = nullptr;
    }
    if (ctx->d_x_scan_angle) {
        cudaFree(ctx->d_x_scan_angle);
        ctx->d_x_scan_angle = nullptr;
    }
    if (ctx->d_y_scan_angle) {
        cudaFree(ctx->d_y_scan_angle);
        ctx->d_y_scan_angle = nullptr;
    }

    delete ctx;
}

// ---------- API ----------
int InitStreamFFT(int N, int BATCH, float dis_para, float velo_para, int B_scannum,
    float* h_x_scan_angle, float* h_y_scan_angle, StreamCtx** out)
{
    if (!out) return 100;
    *out = nullptr;

    if (N <= 1 || BATCH <= 0) return 1;

    int ret = 0;
    auto* ctx = new StreamCtx();

    ctx->N = N;
    ctx->BATCH = BATCH;
    ctx->x_pixel_num = BATCH / 2;
    ctx->B_scannum = B_scannum;
    ctx->K = N / 2 + 1;
    ctx->dis_para = dis_para;
    ctx->velo_para = velo_para;
    ctx->inCount = (size_t)N * (size_t)BATCH;
    ctx->outCount = (size_t)ctx->K * (size_t)BATCH;

    if (!checkCuda(cudaSetDevice(0), ret, "cudaSetDevice")) {
        cleanup(ctx);
        return ret;
    }

    if (!checkCuda(cudaMalloc(&ctx->d_win, sizeof(float) * (size_t)N), ret, "cudaMalloc d_win")) {
        cleanup(ctx);
        return ret;
    }

    if (!checkCuda(cudaMalloc(&ctx->d_x_scan_angle, sizeof(float) * (size_t)ctx->x_pixel_num), ret, "cudaMalloc d_x_scan_angle")) {
        cleanup(ctx);
        return ret;
    }

    if (!checkCuda(cudaMalloc(&ctx->d_y_scan_angle, sizeof(float) * (size_t)B_scannum), ret, "cudaMalloc d_y_scan_angle")) {
        cleanup(ctx);
        return ret;
    }

    for (int s = 0; s < StreamCtx::NSLOT; ++s) {
        auto& sl = ctx->slot[s];

        if (!checkCuda(cudaStreamCreate(&sl.stream), ret, "cudaStreamCreate")) {
            cleanup(ctx);
            return ret;
        }

        if (!checkCuda(cudaEventCreateWithFlags(&sl.h2d_done, cudaEventDisableTiming),
            ret, "cudaEventCreate(h2d_done)")) {
            cleanup(ctx);
            return ret;
        }

        if (!checkCuda(cudaEventCreateWithFlags(&sl.done, cudaEventDisableTiming),
            ret, "cudaEventCreate(done)")) {
            cleanup(ctx);
            return ret;
        }

        if (!checkCuda(cudaMalloc(&sl.d_u16, sizeof(uint16_t) * ctx->inCount), ret, "cudaMalloc d_u16")) {
            cleanup(ctx);
            return ret;
        }

        if (!checkCuda(cudaMalloc(&sl.d_in, sizeof(float) * ctx->inCount), ret, "cudaMalloc d_in")) {
            cleanup(ctx);
            return ret;
        }

        if (!checkCuda(cudaMalloc(&sl.d_out, sizeof(cufftComplex) * ctx->outCount), ret, "cudaMalloc d_out")) {
            cleanup(ctx);
            return ret;
        }

        if (!checkCuda(cudaMalloc(&sl.d_mag, sizeof(float) * ctx->outCount), ret, "cudaMalloc d_mag")) {
            cleanup(ctx);
            return ret;
        }

        if (!checkCuda(cudaMalloc(&sl.d_peak, sizeof(PeakResult) * (size_t)ctx->BATCH), ret, "cudaMalloc d_peak")) {
            cleanup(ctx);
            return ret;
        }

        if (!checkCufft(cufftPlan1d(&sl.plan, N, CUFFT_R2C, BATCH), ret, "cufftPlan1d")) {
            cleanup(ctx);
            return ret;
        }

        if (!checkCufft(cufftSetStream(sl.plan, sl.stream), ret, "cufftSetStream")) {
            cleanup(ctx);
            return ret;
        }

        if (!checkCuda(cudaMalloc(&sl.dis_velo, sizeof(Dis_value) * (size_t)ctx->x_pixel_num), ret, "cudaMalloc dis_velo")) {
            cleanup(ctx);
            return ret;
        }

        sl.in_flight = false;
        sl.h2d_reported = false;
    }

    // Hann window
    {
        int threads = 256;
        int blocks = (N + threads - 1) / threads;

        make_hann << <blocks, threads, 0, ctx->slot[0].stream >> > (ctx->d_win, N);

        if (!checkCuda(cudaGetLastError(), ret, "make_hann launch")) {
            cleanup(ctx);
            return ret;
        }

        if (!checkCuda(cudaStreamSynchronize(ctx->slot[0].stream), ret, "make_hann sync")) {
            cleanup(ctx);
            return ret;
        }
    }

    // X/Y angle transfer
    {
        if (!checkCuda(
            cudaMemcpyAsync(
                ctx->d_x_scan_angle,
                h_x_scan_angle,
                sizeof(float) * (size_t)ctx->x_pixel_num,
                cudaMemcpyHostToDevice,
                ctx->slot[0].stream),
            ret, "H2D x_scan_angle")) {
            cleanup(ctx);
            return ret;
        }

        if (!checkCuda(
            cudaMemcpyAsync(
                ctx->d_y_scan_angle,
                h_y_scan_angle,
                sizeof(float) * (size_t)B_scannum,
                cudaMemcpyHostToDevice,
                ctx->slot[0].stream),
            ret, "H2D y_scan_angle")) {
            cleanup(ctx);
            return ret;
        }

        if (!checkCuda(cudaStreamSynchronize(ctx->slot[0].stream), ret, "angle copy sync")) {
            cleanup(ctx);
            return ret;
        }
    }

    *out = ctx;
    return 0;
}

void CleanupStreamFFT(StreamCtx* ctx) {
    cleanup(ctx);
}

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
    Dis_value* h_dis_out)
{
    if (!ctx || !h_u16_whole || !h_fft_db_K || !h_peak_out || !h_dis_out) return 1;
    if (slot < 0 || slot >= StreamCtx::NSLOT) return 1;

    auto& sl = ctx->slot[slot];
    if (sl.in_flight) return 2;

    r = std::max(0, std::min(r, ctx->BATCH - 1));
    min_index = std::max(0, std::min(min_index, ctx->K - 1));
    max_index = std::max(0, std::min(max_index, ctx->K - 1));

    if (max_index < min_index) {
        std::swap(min_index, max_index);
    }

    const size_t K = (size_t)ctx->K;
    const size_t off = (size_t)r * K;

    int ret = 0;

    // 이전 enqueue 상태 초기화
    sl.h2d_reported = false;

    // 1) H2D whole buffer
    if (!checkCuda(
        cudaMemcpyAsync(
            sl.d_u16,
            h_u16_whole,
            sizeof(uint16_t) * ctx->inCount,
            cudaMemcpyHostToDevice,
            sl.stream),
        ret, "H2D u16 whole"))
    {
        return ret;
    }

    // 2) H2D 완료 이벤트
    if (!checkCuda(
        cudaEventRecord(sl.h2d_done, sl.stream),
        ret, "event record(h2d_done)"))
    {
        return ret;
    }

    // 3) u16 -> f32 + hann
    {
        int threads = 256;
        int blocks = (int)((ctx->inCount + threads - 1) / threads);

        u16_to_f32_hann << <blocks, threads, 0, sl.stream >> > (
            sl.d_u16,
            sl.d_in,
            ctx->d_win,
            ctx->inCount,
            ctx->N);

        if (!checkCuda(cudaGetLastError(), ret, "u16_to_f32_hann launch")) {
            return ret;
        }
    }

    // 4) FFT batch
    if (!checkCufft(
        cufftExecR2C(sl.plan, (cufftReal*)sl.d_in, (cufftComplex*)sl.d_out),
        ret, "cufftExecR2C"))
    {
        return ret;
    }

    // 5) magnitude(dB)
    {
        int threads = 256;
        int blocks = (int)((ctx->outCount + threads - 1) / threads);

        complex_to_mag_db << <blocks, threads, 0, sl.stream >> > (
            sl.d_out,
            sl.d_mag,
            ctx->outCount);

        if (!checkCuda(cudaGetLastError(), ret, "complex_to_mag_db launch")) {
            return ret;
        }
    }

    // 6) peak detect per record
    {
        int threads = 256;
        int blocks = ctx->BATCH;
        size_t smem = sizeof(float) * threads + sizeof(int) * threads;

        detect_peak_per_record << <blocks, threads, smem, sl.stream >> > (
            sl.d_mag,
            ctx->K,
            ctx->BATCH,
            min_index,
            max_index,
            threshold_db,
            sl.d_peak);

        if (!checkCuda(cudaGetLastError(), ret, "detect_peak_per_record launch")) {
            return ret;
        }
    }
    
    // 6-1) xyzv_ calculation
    {
        int pair_count = ctx->BATCH / 2;
        int threads = 256;
        int blocks = (pair_count + threads - 1) / threads;


        dis_velo_calculation << <blocks, threads, 0, sl.stream >> > (
            sl.d_peak,
            sl.dis_velo,
            ctx->d_x_scan_angle,
            ctx->d_y_scan_angle,
            pair_count,
            ctx->dis_para,
            ctx->velo_para,
            direction,
            current_Bscannum,
            ctx->B_scannum
            );

        if (!checkCuda(cudaGetLastError(), ret, "dis_velo_calculation launch")) {
            return ret;
        }

        if (!checkCuda(cudaStreamSynchronize(sl.stream), ret, "dis_velo_calculation sync")) {
            return ret;
        }
    }
    // 7) D2H peak result whole batch
    if (!checkCuda(
        cudaMemcpyAsync(
            h_peak_out,
            sl.d_peak,
            sizeof(PeakResult) * (size_t)ctx->BATCH,
            cudaMemcpyDeviceToHost,
            sl.stream),
        ret, "D2H peak results"))
    {
        return ret;
    }

    //7-1 D2H XYZV result whole batch
    if (!checkCuda(
        cudaMemcpyAsync(
            h_dis_out,
            sl.dis_velo,
            sizeof(Dis_value) * (size_t)ctx->BATCH/2,
            cudaMemcpyDeviceToHost,
            sl.stream),
        ret, "D2H XYVZ results"))
    {
        return ret;
    }

    // 8) D2H only r-th spectrum
    if (!checkCuda(
        cudaMemcpyAsync(
            h_fft_db_K,
            sl.d_mag + off,
            sizeof(float) * K,
            cudaMemcpyDeviceToHost,
            sl.stream),
        ret, "D2H r-th spectrum"))
    {
        return ret;
    }

    // 9) 전체 완료 이벤트
    if (!checkCuda(
        cudaEventRecord(sl.done, sl.stream),
        ret, "event record(done)"))
    {
        return ret;
    }

    sl.in_flight = true;
    return 0;
}

// H2D 완료된 slot 반환
int TryGetH2DDone(StreamCtx* ctx) {
    if (!ctx) return -1;

    for (int s = 0; s < StreamCtx::NSLOT; ++s) {
        auto& sl = ctx->slot[s];

        if (!sl.in_flight) continue;
        if (sl.h2d_reported) continue;

        cudaError_t q = cudaEventQuery(sl.h2d_done);
        if (q == cudaSuccess) {
            sl.h2d_reported = true;
            return s;
        }
        if (q != cudaErrorNotReady) {
            std::fprintf(stderr, "[CUDA] cudaEventQuery(h2d_done) failed on slot %d: %s\n",
                s, cudaGetErrorString(q));
        }
    }

    return -1;
}

// 전체 완료된 slot 반환
int TryDequeue(StreamCtx* ctx) {
    if (!ctx) return -1;

    for (int s = 0; s < StreamCtx::NSLOT; ++s) {
        auto& sl = ctx->slot[s];

        if (!sl.in_flight) continue;

        cudaError_t q = cudaEventQuery(sl.done);
        if (q == cudaSuccess) {
            sl.in_flight = false;
            sl.h2d_reported = false;
            return s;
        }
        if (q != cudaErrorNotReady) {
            std::fprintf(stderr, "[CUDA] cudaEventQuery(done) failed on slot %d: %s\n",
                s, cudaGetErrorString(q));
        }
    }

    return -1;
}
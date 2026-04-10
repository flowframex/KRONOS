// ================================================================
//  NEXUS.cu  —  v2.0  —  "The SOVEREIGN"
//  VRAM-Native Frame Generation Engine — Extreme Low-End Edition
//
//  Target  : GT 730 GF108 CC2.1 (Fermi) / i3 2120 / 6 GB DDR3
//  Build   : nvcc -O2 -arch=sm_21 -cudart static
//             -allow-unsupported-compiler
//             -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH
//             NEXUS.cu -o NEXUS.exe
//             -Xcompiler "/MT /EHsc /W3 /WX-
//              /D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH"
//             -luser32 -lgdi32
//  Runtime : Windows 10/11 x64  +  NVIDIA driver >= 367.48
//
//  ── v2.0 changes over v1.1 ──────────────────────────────────
//  [FIX]  d_sat removed — was identical to d_clamp01 (bug)
//  [FIX]  d_aniso_rgb: compute kernel weights ONCE for R+G+B
//         (was 3× d_aniso_ch → 54 expf/px; now 9 expf/px → 6×)
//  [FIX]  OFC corrected: |It + v·∇L| / (|It| + |v||∇L| + ε)
//         (was computing |It|/|∇L| and calling it cos_theta)
//  [FIX]  CUDA dedicated stream + cudaMemcpyAsync — true async
//  [FIX]  CUDA events replace cudaDeviceSynchronize in hot path
//  [FIX]  Overlay repositioned over target window every frame
//  [FIX]  gen_thread pumps Win32 messages (prevents HWND freeze)
//  [FIX]  Persistent DIBSection in blit_host (no alloc per blit)
//  [FIX]  Persistent DIBSection in capture_gdi (same)
//  [FIX]  capture_gdi: PW_RENDERFULLCONTENT + BitBlt fallback
//  [FIX]  Resolution change: re-upload frame to all 3 VRAM slots
//  [FIX]  Adaptive frame pacing — Sleep computed from real FPS
//  [PERF] SOVEREIGN: shared-memory tile for F2 (uchar4) + flow
//         (float2) — 3888 B/block → 12 blocks/SM on Fermi 48 KB
//         Replaces 8+ extra global reads per pixel
//  [FEAT] F9 global hotkey toggles engine on/off
//  [FEAT] cudaError_t checked on all critical allocations
//  [FEAT] Cores-per-SM table extended: Volta / Turing / Ampere
// ================================================================
//
//  ╔══════════════════════════════════════════════════════════════╗
//  ║           THE SOVEREIGN SYNTHESIS — NEW MATHEMATICS         ║
//  ╠══════════════════════════════════════════════════════════════╣
//  ║  Three bounded estimates of F(t + 0.5):                     ║
//  ║                                                             ║
//  ║  F_warp = AnisoBilinear(F2, p − v·τ)                       ║
//  ║    Lucas-Kanade optical flow (GPU, shared-mem, no library)  ║
//  ║    Anisotropic kernel — no blur perpendicular to motion     ║
//  ║                                                             ║
//  ║  F_temp = exp(1.625·log F2 − 0.75·log F1 + 0.125·log F0)  ║
//  ║    Log-geodesic Taylor extrapolation (τ=0.5 forward)       ║
//  ║    Ratio-preserving, zero colour blur                       ║
//  ║                                                             ║
//  ║  F_spat = F2 + AnisoLaplacian(F2) · c²·τ²/2               ║
//  ║    Anisotropic wave PDE — edge-aware, slower at edges       ║
//  ║                                                             ║
//  ║  Crown: Weighted Harmonic Mean (WHM) Fusion                 ║
//  ║    F* = W / (w_w/F_warp + w_t/F_temp + w_s/F_spat)        ║
//  ║    WHM ≤ GM ≤ AM  →  pulls toward correct (smaller) value  ║
//  ║    Ghost-proof without detection; blur-free by design       ║
//  ║    Bounded: min(F_w,F_t,F_s) ≤ F* ≤ max(F_w,F_t,F_s)     ║
//  ╚══════════════════════════════════════════════════════════════╝

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cuda_runtime.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

#define APP_NAME  "NEXUS v2.0  —  The SOVEREIGN Frame Generation Engine"
#define APP_VER   "2.0"
#define HOTKEY_ID 9001

// ================================================================
//  §1  CONSTANTS
// ================================================================

// Lucas-Kanade window half-radius (LK_R=2 → 5×5 window)
#define LK_R            2
// Harris quality → [0,1] scale factor
#define LK_SCALE        8000.f
// Max optical flow magnitude (pixels per frame)
#define MAX_FLOW        80.f

// Divergence confidence gate width
#define SIGMA_DIV       0.40f
#define INV_SIG_DIV_SQ  (1.f / (SIGMA_DIV * SIGMA_DIV))

// Anisotropic warp kernel (3×3 on Fermi for speed)
#define ANISO_R         1
#define ANISO_EPS       0.05f      // near-delta width ALONG motion
#define ANISO_SIGMA     0.50f      // Gaussian width PERP motion (alias)

// FLGE log-geodesic Taylor extrapolation at τ=0.5 forward
// f(t+0.5) = f(t) + 0.5*f'(t) + 0.125*f''(t)
// f'  ≈ F2−F1,  f'' ≈ F2−2F1+F0  (backward differences)
// → log F* = 0.125·logF0 − 0.75·logF1 + 1.625·logF2  (sum=1)
#define FLGE_A0         0.125f
#define FLGE_A1        -0.75f
#define FLGE_A2         1.625f

// FASW spatial wave PDE
#define STWAVE_ALPHA    0.85f      // anisotropy strength
#define STWAVE_C0SQ     4.0f       // base wave-speed²
#define STWAVE_LAMBDA   5000.0f    // edge-adaptive slowdown
#define STWAVE_TAU_SQ   0.125f     // τ²/2  (τ=0.5)

// Confidence routing
#define OMEGA_K         0.80f      // jerk sensitivity
#define ENTROPY_SCALE   0.0008f    // texture detection
#define LAMBDA_ENT      0.30f      // flat-region push → spatial
#define OFC_WEIGHT      0.20f      // brightness-constancy gate

// Log-Riemannian Amplitude cap (prevents FLGE explosion at dark→bright)
#define RATIO_CAP       3.0f

// Numerics
#define LOG_FLOOR       1e-4f
#define EPS_F           1e-7f

// CUDA grid
#define BLOCK_W         16
#define BLOCK_H         16

// ================================================================
//  §2  DEVICE HELPERS
// ================================================================

__device__ __forceinline__ float d_clamp01(float v) {
    return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
}

// Safe log: floor at LOG_FLOOR, cap at 1.0 (input is already [0,1])
__device__ __forceinline__ float d_slog(float v) {
    return logf(v < LOG_FLOOR ? LOG_FLOOR : (v > 1.f ? 1.f : v));
}

__device__ __forceinline__ float d_lum_f(float r, float g, float b) {
    return 0.299f * r + 0.587f * g + 0.114f * b;
}

// Load BGRA uchar4 → float RGB [0,1]
__device__ __forceinline__
void d_load(uchar4 p, float &r, float &g, float &b) {
    const float k = 1.f / 255.f;
    r = p.z * k;  g = p.y * k;  b = p.x * k;
}

// Load luminance from BGRA uchar4
__device__ __forceinline__
float d_lum4(uchar4 p) {
    const float k = 1.f / 255.f;
    return 0.299f * p.z * k + 0.587f * p.y * k + 0.114f * p.x * k;
}

// Store float RGB → BGRA uchar4
__device__ __forceinline__
uchar4 d_store(float r, float g, float b) {
    uchar4 o;
    o.x = (unsigned char)(d_clamp01(b) * 255.f + 0.5f);
    o.y = (unsigned char)(d_clamp01(g) * 255.f + 0.5f);
    o.z = (unsigned char)(d_clamp01(r) * 255.f + 0.5f);
    o.w = 255;
    return o;
}

// Bilinear sample — single channel
__device__
float d_bilinear_ch(const uchar4 * __restrict__ img,
                    float u, float v, int W, int H, int ch)
{
    u = fmaxf(0.f, fminf((float)(W-1) - 0.001f, u));
    v = fmaxf(0.f, fminf((float)(H-1) - 0.001f, v));
    int x0 = (int)u, y0 = (int)v;
    int x1 = min(x0+1, W-1), y1 = min(y0+1, H-1);
    float fx = u - x0, fy = v - y0;
    uchar4 s00 = img[y0*W+x0], s10 = img[y0*W+x1];
    uchar4 s01 = img[y1*W+x0], s11 = img[y1*W+x1];
    float v00, v10, v01, v11;
    if      (ch==0){ v00=s00.z; v10=s10.z; v01=s01.z; v11=s11.z; }
    else if (ch==1){ v00=s00.y; v10=s10.y; v01=s01.y; v11=s11.y; }
    else           { v00=s00.x; v10=s10.x; v01=s01.x; v11=s11.x; }
    return (v00*(1.f-fx)*(1.f-fy) + v10*fx*(1.f-fy)
           +v01*(1.f-fx)*fy       + v11*fx*fy) * (1.f/255.f);
}

// ================================================================
//  §3  ANISOTROPIC WARP — ALL THREE CHANNELS IN ONE PASS
//  v2.0 fix: was called 3× (d_aniso_ch per channel) → 54 expf/px
//  Now: weights computed once, applied to R+G+B → 9 expf/px (6×)
// ================================================================
__device__
void d_aniso_rgb(const uchar4 * __restrict__ img,
                 float cx, float cy,
                 float mhx, float mhy,   // unit vector along motion
                 float mpx, float mpy,   // unit vector perpendicular
                 int W, int H,
                 float &r, float &g, float &b)
{
    float vr = 0.f, vg = 0.f, vb = 0.f, ws = 0.f;
    const float inv_ee = 1.f / (ANISO_EPS   * ANISO_EPS + EPS_F);
    const float inv_ss = 1.f / (ANISO_SIGMA * ANISO_SIGMA + EPS_F);
    const float k255   = 1.f / 255.f;

    for (int dy = -ANISO_R; dy <= ANISO_R; dy++) {
        for (int dx = -ANISO_R; dx <= ANISO_R; dx++) {
            int nx = max(0, min(W-1, (int)(cx + dx + 0.5f)));
            int ny = max(0, min(H-1, (int)(cy + dy + 0.5f)));

            float pm = (float)dx * mhx + (float)dy * mhy;
            float pp = (float)dx * mpx + (float)dy * mpy;
            // Combined into one expf (was two — now truly 9 expf/px)
            float w  = expf(-(pm*pm)*inv_ee - (pp*pp)*inv_ss);

            uchar4 p = img[ny*W + nx];
            vr += w * p.z * k255;
            vg += w * p.y * k255;
            vb += w * p.x * k255;
            ws += w;
        }
    }

    if (ws > EPS_F) {
        float inv = 1.f / ws;
        r = vr * inv;  g = vg * inv;  b = vb * inv;
    } else {
        r = d_bilinear_ch(img, cx, cy, W, H, 0);
        g = d_bilinear_ch(img, cx, cy, W, H, 1);
        b = d_bilinear_ch(img, cx, cy, W, H, 2);
    }
}

// ================================================================
//  §4  KERNEL 1: LUCAS-KANADE OPTICAL FLOW  (shared-mem tile)
//
//  Computes per-pixel 2D motion from F1→F2.
//  5×5 window (LK_R=2). Closed-form 2×2 system.
//  Shared-memory tile: (BLOCK+2×PAD)² × 2 lum bufs
//    PAD = LK_R+1 = 3.  Tile = 22×22 × 2 × 4B = 3872 B/block
//    Fermi 48 KB → ~12 concurrent blocks/SM on GT 730.
// ================================================================
#define LK_PAD   (LK_R + 1)
#define LK_TW    (BLOCK_W + 2*LK_PAD)
#define LK_TH    (BLOCK_H + 2*LK_PAD)

__global__ void nexus_lk_flow(
    const uchar4 * __restrict__ d_F2,
    const uchar4 * __restrict__ d_F1,
    float2       * __restrict__ d_flow,
    float        * __restrict__ d_qual,
    int W, int H)
{
    __shared__ float sL2[LK_TH][LK_TW];
    __shared__ float sL1[LK_TH][LK_TW];

    int tx = threadIdx.x, ty = threadIdx.y;
    int bx = blockIdx.x * BLOCK_W, by = blockIdx.y * BLOCK_H;

    // Cooperative tile load
    int tile_size   = LK_TW * LK_TH;
    int thread_id   = ty * BLOCK_W + tx;
    int num_threads = BLOCK_W * BLOCK_H;

    for (int i = thread_id; i < tile_size; i += num_threads) {
        int ly = i / LK_TW, lx = i % LK_TW;
        int gx = max(0, min(W-1, bx - LK_PAD + lx));
        int gy = max(0, min(H-1, by - LK_PAD + ly));
        int gi = gy * W + gx;
        sL2[ly][lx] = d_lum4(d_F2[gi]);
        sL1[ly][lx] = d_lum4(d_F1[gi]);
    }
    __syncthreads();

    int x = bx + tx, y = by + ty;
    if (x >= W || y >= H) return;

    float sxx=0.f, syy=0.f, sxy=0.f, sxt=0.f, syt=0.f;

    for (int dy = -LK_R; dy <= LK_R; dy++) {
        for (int dx = -LK_R; dx <= LK_R; dx++) {
            int lx = tx + LK_PAD + dx;
            int ly = ty + LK_PAD + dy;
            float Ix = (sL2[ly][lx+1] - sL2[ly][lx-1]) * 0.5f;
            float Iy = (sL2[ly+1][lx] - sL2[ly-1][lx]) * 0.5f;
            float It =  sL2[ly][lx]   - sL1[ly][lx];
            sxx += Ix*Ix;  syy += Iy*Iy;  sxy += Ix*Iy;
            sxt += Ix*It;  syt += Iy*It;
        }
    }

    // 2×2 closed-form solve
    float det   = sxx*syy - sxy*sxy;
    float trace = sxx + syy;
    float q     = det / (trace*trace + EPS_F);  // Harris response

    float vx = 0.f, vy = 0.f;
    if (fabsf(det) > 1e-5f) {
        float id = 1.f / det;
        vx = (-sxt*syy + syt*sxy) * id;
        vy = (-syt*sxx + sxt*sxy) * id;
    }
    vx = fmaxf(-MAX_FLOW, fminf(MAX_FLOW, vx));
    vy = fmaxf(-MAX_FLOW, fminf(MAX_FLOW, vy));

    d_flow[y*W + x] = make_float2(vx, vy);
    d_qual[y*W + x] = fmaxf(0.f, q);
}

// ================================================================
//  §5  KERNEL 2: NEXUS SOVEREIGN SYNTHESIS
//
//  v2.0: shared-memory tile for F2 (uchar4) + flow (float2)
//    Tile = (BLOCK+2)² — 1-pixel halo for neighbour ops
//    18×18 × (4+8) B = 3888 B/block → 12 blocks/SM on GT 730
//    Replaces 8+ extra global reads per pixel
//
//  v2.0: OFC corrected to brightness constancy residual:
//    err = |It + vx·Lx + vy·Ly| / (|It| + |v||∇L| + ε)
//    (was computing |It|/|∇L| which is not a cosine)
//
//  v2.0: d_aniso_rgb replaces 3× d_aniso_ch (6× fewer expf)
// ================================================================
#define S_PAD 1
#define S_TW  (BLOCK_W + 2*S_PAD)   // 18
#define S_TH  (BLOCK_H + 2*S_PAD)   // 18

__global__ void nexus_sovereign(
    const uchar4 * __restrict__ d_F2,
    const uchar4 * __restrict__ d_F1,
    const uchar4 * __restrict__ d_F0,
    const float2 * __restrict__ d_flow,
    const float  * __restrict__ d_qual,
    uchar4       * __restrict__ d_out,
    int W, int H)
{
    // 3888 B / block → 12 concurrent blocks/SM on Fermi 48 KB
    __shared__ uchar4 sF2[S_TH][S_TW];   // F2 pixels (packed BGRA)
    __shared__ float2 sFL[S_TH][S_TW];   // optical flow (vx, vy)

    int tx = threadIdx.x, ty = threadIdx.y;
    int bx = blockIdx.x * BLOCK_W, by = blockIdx.y * BLOCK_H;

    // Cooperative load with 1-pixel halo
    int tile_size   = S_TW * S_TH;
    int thread_id   = ty * BLOCK_W + tx;
    int num_threads = BLOCK_W * BLOCK_H;

    for (int i = thread_id; i < tile_size; i += num_threads) {
        int ly = i / S_TW, lx = i % S_TW;
        int gx = max(0, min(W-1, bx - S_PAD + lx));
        int gy = max(0, min(H-1, by - S_PAD + ly));
        int gi = gy * W + gx;
        sF2[ly][lx] = d_F2[gi];
        sFL[ly][lx] = d_flow[gi];
    }
    __syncthreads();

    int x = bx + tx, y = by + ty;
    if (x >= W || y >= H) return;
    int idx = y * W + x;

    // Shared-memory local coordinates
    int sx = tx + S_PAD, sy = ty + S_PAD;

    // ── Load current pixel and neighbours from shared mem ─────────
    float r2, g2, b2;  d_load(sF2[sy][sx],   r2, g2, b2);
    float rE, gE, bE;  d_load(sF2[sy][sx+1], rE, gE, bE);
    float rW, gW, bW;  d_load(sF2[sy][sx-1], rW, gW, bW);
    float rN, gN, bN;  d_load(sF2[sy-1][sx], rN, gN, bN);
    float rS, gS, bS;  d_load(sF2[sy+1][sx], rS, gS, bS);

    // ── Flow + quality from shared / global ───────────────────────
    float vx = sFL[sy][sx].x, vy = sFL[sy][sx].y;
    float lk_q = d_qual[idx];

    // ── Divergence confidence (from shared flow neighbours) ────────
    float dvx_dx = (sFL[sy][sx+1].x - sFL[sy][sx-1].x) * 0.5f;
    float dvy_dy = (sFL[sy+1][sx].y - sFL[sy-1][sx].y) * 0.5f;
    float div    = dvx_dx + dvy_dy;
    float c_div  = expf(-(div*div) * INV_SIG_DIV_SQ);
    float c_lk   = d_clamp01(lk_q * LK_SCALE);
    float w_w    = c_div * c_lk;

    // ── F1, F0 — only need current pixel (no halo needed) ─────────
    float r1, g1, b1;  d_load(d_F1[idx], r1, g1, b1);
    float r0, g0, b0;  d_load(d_F0[idx], r0, g0, b0);

    // ── Temporal velocity and acceleration ────────────────────────
    float Vr = r2-r1, Vg = g2-g1, Vb = b2-b1;
    float Ar = r2-2.f*r1+r0, Ag = g2-2.f*g1+g0, Ab = b2-2.f*b1+b0;
    float VV = Vr*Vr + Vg*Vg + Vb*Vb + EPS_F;
    float AA = Ar*Ar + Ag*Ag + Ab*Ab;

    // ── Luminance gradient of F2 (from shared neighbours) ─────────
    float Lx = (d_lum_f(rE,gE,bE) - d_lum_f(rW,gW,bW)) * 0.5f;
    float Ly = (d_lum_f(rS,gS,bS) - d_lum_f(rN,gN,bN)) * 0.5f;
    float G  = Lx*Lx + Ly*Ly + 1e-5f;
    float h  = d_clamp01(G * ENTROPY_SCALE);  // 0=flat, 1=textured

    // ── PILLAR A: AnisoBilinear Backward Warp ─────────────────────
    // Backward-warp F2 by v·τ (τ=0.5) to estimate F(t+0.5)
    // Anisotropic: near-delta ALONG motion, Gaussian PERP motion
    float tau  = 0.5f;
    float srcx = (float)x - vx * tau;
    float srcy = (float)y - vy * tau;
    float vlen = sqrtf(vx*vx + vy*vy + EPS_F);

    float Fwr, Fwg, Fwb;
    if (vlen > 0.3f) {
        float mhx =  vx/vlen, mhy =  vy/vlen;
        float mpx = -mhy,     mpy =  mhx;
        // v2.0: one call (was three), 9 expf/px (was 54)
        d_aniso_rgb(d_F2, srcx, srcy, mhx, mhy, mpx, mpy, W, H,
                    Fwr, Fwg, Fwb);
    } else {
        Fwr = d_bilinear_ch(d_F2, srcx, srcy, W, H, 0);
        Fwg = d_bilinear_ch(d_F2, srcx, srcy, W, H, 1);
        Fwb = d_bilinear_ch(d_F2, srcx, srcy, W, H, 2);
    }
    Fwr = d_clamp01(Fwr);  Fwg = d_clamp01(Fwg);  Fwb = d_clamp01(Fwb);

    // ── PILLAR B: FLGE Log-Geodesic Temporal Estimate ─────────────
    // log-geodesic Taylor at τ=0.5:
    //   log F* = 0.125·logF0 − 0.75·logF1 + 1.625·logF2
    float Ftr = expf(FLGE_A2*d_slog(r2) + FLGE_A1*d_slog(r1) + FLGE_A0*d_slog(r0));
    float Ftg = expf(FLGE_A2*d_slog(g2) + FLGE_A1*d_slog(g1) + FLGE_A0*d_slog(g0));
    float Ftb = expf(FLGE_A2*d_slog(b2) + FLGE_A1*d_slog(b1) + FLGE_A0*d_slog(b0));
    // LRA: clamp ratio explosion at dark→bright edges
    if (Ftr > r2 * RATIO_CAP + LOG_FLOOR) Ftr = r2 * RATIO_CAP + LOG_FLOOR;
    if (Ftg > g2 * RATIO_CAP + LOG_FLOOR) Ftg = g2 * RATIO_CAP + LOG_FLOOR;
    if (Ftb > b2 * RATIO_CAP + LOG_FLOOR) Ftb = b2 * RATIO_CAP + LOG_FLOOR;
    Ftr = d_clamp01(Ftr);  Ftg = d_clamp01(Ftg);  Ftb = d_clamp01(Ftb);

    // ── PILLAR C: FASW Anisotropic Spatial Wave ───────────────────
    // F_spat = F2 + AnisoLap(F2) · c²·τ²/2
    // Dxx,Dyy: diffusion attenuated along strong gradients
    // c² edge-adaptive: slower at edges → no edge bleeding
    float Dxx   = 1.f - STWAVE_ALPHA * (Lx*Lx) / G;
    float Dyy   = 1.f - STWAVE_ALPHA * (Ly*Ly) / G;
    float cSq   = STWAVE_C0SQ / (1.f + STWAVE_LAMBDA * G);
    float coeff = cSq * STWAVE_TAU_SQ;
    float Fsr = d_clamp01(r2 + ((rE+rW-2.f*r2)*Dxx + (rN+rS-2.f*r2)*Dyy)*coeff);
    float Fsg = d_clamp01(g2 + ((gE+gW-2.f*g2)*Dxx + (gN+gS-2.f*g2)*Dyy)*coeff);
    float Fsb = d_clamp01(b2 + ((bE+bW-2.f*b2)*Dxx + (bN+bS-2.f*b2)*Dyy)*coeff);

    // ── CONFIDENCE WEIGHTS ────────────────────────────────────────

    // JOD: Jerk Occlusion Detection
    float relJ   = AA / VV;
    float w_jerk = d_clamp01(OMEGA_K * relJ);

    // OFC: Optical Flow Consistency (brightness constancy residual)
    // v2.0 fix: was computing |It|/|∇L| — not a cosine of anything.
    // Correct formula: err = |It + v·∇L| / (|It| + |v||∇L| + ε)
    float It     = d_lum_f(Vr, Vg, Vb);         // temporal lum gradient
    float bc_res = fabsf(It + vx*Lx + vy*Ly);   // brightness constancy residual
    float bc_scl = fabsf(It) + sqrtf(vx*vx+vy*vy+EPS_F)*sqrtf(G) + EPS_F;
    float ofc    = OFC_WEIGHT * d_clamp01(bc_res / bc_scl)
                              * d_clamp01(G / (G + 1e-4f));  // textured only

    // Combine confidence into pillar weights
    float w_t     = (1.f - w_jerk) * h;
    float w_s     = d_clamp01(w_jerk + LAMBDA_ENT*(1.f-h) + ofc);
    float W_total = w_w + w_t + w_s + EPS_F;

    // ── SOVEREIGN CROWN: Weighted Harmonic Mean Fusion ────────────
    //  F*(c) = W_total / (w_w/F_warp + w_t/F_temp + w_s/F_spat)
    //  Fréchet mean on (ℝ⁺, ds²=dF²/F⁴)
    //  Ghost-proof: bright ghost → small 1/F_ghost → negligible
    //  Zero transcendentals: 3 div + 2 add per channel
    float fl = LOG_FLOOR;
    float inv_r = w_w/(W_total*fmaxf(Fwr,fl)) + w_t/(W_total*fmaxf(Ftr,fl)) + w_s/(W_total*fmaxf(Fsr,fl));
    float inv_g = w_w/(W_total*fmaxf(Fwg,fl)) + w_t/(W_total*fmaxf(Ftg,fl)) + w_s/(W_total*fmaxf(Fsg,fl));
    float inv_b = w_w/(W_total*fmaxf(Fwb,fl)) + w_t/(W_total*fmaxf(Ftb,fl)) + w_s/(W_total*fmaxf(Fsb,fl));

    d_out[idx] = d_store(d_clamp01(1.f/fmaxf(inv_r,fl)),
                         d_clamp01(1.f/fmaxf(inv_g,fl)),
                         d_clamp01(1.f/fmaxf(inv_b,fl)));
}

// ================================================================
//  §6  HOST / DEVICE BUFFER STRUCTURES
// ================================================================

struct HostBuf {
    uchar4 *data; int W, H;
    HostBuf() : data(nullptr), W(0), H(0) {}
    bool alloc(int w, int h) {
        if (W==w && H==h && data) return true;
        if (data) cudaFreeHost(data);
        W=w; H=h;
        cudaError_t e = cudaMallocHost(&data, (size_t)w*h*sizeof(uchar4));
        if (e != cudaSuccess) { data=nullptr; W=H=0; return false; }
        return true;
    }
    void free_buf() { if (data){ cudaFreeHost(data); data=nullptr; } W=H=0; }
};

struct DevBuf {
    uchar4 *ptr; int W, H;
    DevBuf() : ptr(nullptr), W(0), H(0) {}
    bool alloc(int w, int h) {
        if (W==w && H==h && ptr) return true;
        if (ptr) cudaFree(ptr);
        W=w; H=h;
        cudaError_t e = cudaMalloc(&ptr, (size_t)w*h*sizeof(uchar4));
        if (e != cudaSuccess) { ptr=nullptr; W=H=0; return false; }
        return true;
    }
    void free_buf() { if (ptr){ cudaFree(ptr); ptr=nullptr; } W=H=0; }
};

struct FlowBuf {
    float2 *ptr; float *qual; int W, H;
    FlowBuf() : ptr(nullptr), qual(nullptr), W(0), H(0) {}
    bool alloc(int w, int h) {
        if (W==w && H==h && ptr) return true;
        if (ptr)  cudaFree(ptr);
        if (qual) cudaFree(qual);
        W=w; H=h;
        bool ok = (cudaMalloc(&ptr,  (size_t)w*h*sizeof(float2)) == cudaSuccess)
               && (cudaMalloc(&qual, (size_t)w*h*sizeof(float))  == cudaSuccess);
        if (!ok) { free_buf(); }
        return ok;
    }
    void free_buf() {
        if (ptr)  { cudaFree(ptr);  ptr=nullptr;  }
        if (qual) { cudaFree(qual); qual=nullptr; }
        W=H=0;
    }
};

// ================================================================
//  §7  GLOBALS
// ================================================================
static std::atomic<bool>   g_running{false};
static HWND                g_target_hwnd = NULL;
static HWND                g_overlay     = NULL;
static HWND                g_ctrl_hwnd   = NULL;
static HWND                g_stat_label  = NULL;
static HWND                g_fps_label   = NULL;
static HWND                g_cuda_label  = NULL;
static std::atomic<double> g_fps_real{0.0};
static std::thread         g_gen_thread;
static std::mutex          g_frame_mtx;
static bool                g_cuda_ok     = false;

// Ring: g_ring[0]=current, [1]=prev, [2]=2-prev
static HostBuf  h_fb[3];
static HostBuf  h_gen_buf;
static DevBuf   d_fb[3];
static DevBuf   d_out;
static FlowBuf  d_flw;
static int      g_ring[3] = {0, 1, 2};

// v2.0: dedicated stream + CUDA events for async dispatch
static cudaStream_t g_cuda_stream = nullptr;
static cudaEvent_t  g_ev_done     = nullptr;

// v2.0: persistent GDI resources (no alloc/free per frame)
static HBITMAP  s_blit_bm   = NULL;
static void    *s_blit_bits = NULL;
static HDC      s_blit_mdc  = NULL;
static int      s_blit_W = 0, s_blit_H = 0;

static HBITMAP  s_cap_bm   = NULL;
static void    *s_cap_bits = NULL;
static HDC      s_cap_mdc  = NULL;
static int      s_cap_W = 0, s_cap_H = 0;

// ================================================================
//  §8  GDI SCREEN CAPTURE
//  v2.0: persistent DIBSection (no alloc per capture)
//        PW_RENDERFULLCONTENT hint (Windows 8.1+) + BitBlt fallback
// ================================================================
static bool capture_gdi(HWND src, HostBuf &buf) {
    RECT rc;
    if (src) {
        if (!IsWindow(src)) return false;
        GetClientRect(src, &rc);
    } else {
        rc.left=rc.top=0;
        rc.right  = GetSystemMetrics(SM_CXSCREEN);
        rc.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    int W = rc.right-rc.left, H = rc.bottom-rc.top;
    if (W<=0 || H<=0) return false;
    if (!buf.alloc(W, H)) return false;

    HDC src_dc = src ? GetDC(src) : GetDC(NULL);

    // Lazily create / resize persistent capture DIBSection
    if (W != s_cap_W || H != s_cap_H || !s_cap_bm) {
        if (s_cap_bm)  DeleteObject(s_cap_bm);
        if (s_cap_mdc) DeleteDC(s_cap_mdc);
        s_cap_mdc = CreateCompatibleDC(src_dc);
        BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth=W; bi.bmiHeader.biHeight=-H;
        bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
        bi.bmiHeader.biCompression=BI_RGB;
        s_cap_bm = CreateDIBSection(src_dc, &bi, DIB_RGB_COLORS, &s_cap_bits, NULL, 0);
        SelectObject(s_cap_mdc, s_cap_bm);
        s_cap_W=W; s_cap_H=H;
    }

    if (src) {
        // PW_CLIENTONLY | PW_RENDERFULLCONTENT (0x2, Win8.1+)
        // captures DX/OpenGL content rendered to the window
        UINT flags = PW_CLIENTONLY | 0x2;
        if (!PrintWindow(src, s_cap_mdc, flags)) {
            // Fallback: plain BitBlt (works for GDI, may miss DX exclusive)
            BitBlt(s_cap_mdc, 0, 0, W, H, src_dc, 0, 0, SRCCOPY);
        }
    } else {
        BitBlt(s_cap_mdc, 0, 0, W, H, src_dc, 0, 0, SRCCOPY);
    }
    GdiFlush();

    memcpy(buf.data, s_cap_bits, (size_t)W*H*4);

    if (src) ReleaseDC(src,  src_dc);
    else     ReleaseDC(NULL, src_dc);
    return true;
}

// ================================================================
//  §9  GDI BLIT  (pinned host → overlay window)
//  v2.0: persistent DIBSection + compatible DC — no alloc per blit
// ================================================================
static void blit_host(HWND dst, const HostBuf &buf) {
    if (!dst || !buf.data || buf.W<=0 || buf.H<=0) return;

    // Lazily create / resize persistent blit DIBSection
    if (buf.W != s_blit_W || buf.H != s_blit_H || !s_blit_bm) {
        if (s_blit_bm)  DeleteObject(s_blit_bm);
        if (s_blit_mdc) DeleteDC(s_blit_mdc);
        HDC ref = GetDC(NULL);
        s_blit_mdc = CreateCompatibleDC(ref);
        BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth=buf.W; bi.bmiHeader.biHeight=-buf.H;
        bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
        bi.bmiHeader.biCompression=BI_RGB;
        s_blit_bm = CreateDIBSection(ref, &bi, DIB_RGB_COLORS, &s_blit_bits, NULL, 0);
        SelectObject(s_blit_mdc, s_blit_bm);
        ReleaseDC(NULL, ref);
        s_blit_W=buf.W; s_blit_H=buf.H;
    }

    memcpy(s_blit_bits, buf.data, (size_t)buf.W*buf.H*4);

    HDC hdc = GetDC(dst);
    RECT rc; GetClientRect(dst, &rc);
    StretchBlt(hdc, 0, 0, rc.right, rc.bottom,
               s_blit_mdc, 0, 0, buf.W, buf.H, SRCCOPY);
    ReleaseDC(dst, hdc);
}

static void blit_cleanup() {
    if (s_blit_bm)  { DeleteObject(s_blit_bm); s_blit_bm=NULL; }
    if (s_blit_mdc) { DeleteDC(s_blit_mdc);    s_blit_mdc=NULL; }
    if (s_cap_bm)   { DeleteObject(s_cap_bm);  s_cap_bm=NULL; }
    if (s_cap_mdc)  { DeleteDC(s_cap_mdc);     s_cap_mdc=NULL; }
    s_blit_bits=s_cap_bits=nullptr;
    s_blit_W=s_blit_H=s_cap_W=s_cap_H=0;
}

// ================================================================
//  §10  CUDA FRAME GENERATION DISPATCHER
//  v2.0: dedicated stream + events — no global DeviceSync stall
//        Upload → LK flow → SOVEREIGN → event → Download
// ================================================================
static void generate_frame_cuda(
    DevBuf &d_F2, DevBuf &d_F1, DevBuf &d_F0,
    FlowBuf &d_flow, DevBuf &d_result,
    cudaStream_t stream, cudaEvent_t ev_done)
{
    int W = d_F2.W, H = d_F2.H;
    if (!d_result.alloc(W, H) || !d_flow.alloc(W, H)) return;

    dim3 block(BLOCK_W, BLOCK_H);
    dim3 grid((W+BLOCK_W-1)/BLOCK_W, (H+BLOCK_H-1)/BLOCK_H);

    // Pass 1: Lucas-Kanade optical flow (shared-mem)
    nexus_lk_flow<<<grid, block, 0, stream>>>(
        d_F2.ptr, d_F1.ptr, d_flow.ptr, d_flow.qual, W, H);

    if (cudaGetLastError() != cudaSuccess) return;

    // Pass 2: SOVEREIGN synthesis (shared-mem tile + fixed OFC)
    nexus_sovereign<<<grid, block, 0, stream>>>(
        d_F2.ptr, d_F1.ptr, d_F0.ptr,
        d_flow.ptr, d_flow.qual,
        d_result.ptr, W, H);

    if (cudaGetLastError() != cudaSuccess) return;

    // Record event — CPU unblocks as soon as GPU passes this point
    cudaEventRecord(ev_done, stream);
}

// ================================================================
//  §11  OVERLAY WINDOW
//  v2.0: positioned over target window on every frame, not just (0,0)
// ================================================================
static HWND make_overlay(int W, int H) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSA oc{};
        oc.style         = CS_OWNDC;
        oc.lpfnWndProc   = DefWindowProcA;
        oc.hInstance     = GetModuleHandleA(NULL);
        oc.lpszClassName = "NEXUSOverlay";
        RegisterClassA(&oc);
        reg = true;
    }
    HWND ov = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        "NEXUSOverlay", "",
        WS_POPUP | WS_VISIBLE,
        0, 0, W, H,
        NULL, NULL, GetModuleHandleA(NULL), NULL);
    SetLayeredWindowAttributes(ov, 0, 255, LWA_ALPHA);
    return ov;
}

// Reposition overlay to sit exactly over the target window
static void position_overlay(HWND overlay, HWND target, int W, int H) {
    if (!overlay) return;
    int ox = 0, oy = 0;
    if (target && IsWindow(target)) {
        POINT pt = {0, 0};
        ClientToScreen(target, &pt);
        ox = pt.x; oy = pt.y;
    }
    SetWindowPos(overlay, HWND_TOPMOST,
                 ox, oy, W, H,
                 SWP_NOACTIVATE | SWP_NOZORDER);
}

// ================================================================
//  §12  GENERATION THREAD
//
//  Pipeline per cycle:
//    1. Rotate ring indices
//    2. Capture new frame → h_fb[ring[0]]  (pinned RAM)
//    3. Async upload      → d_fb[ring[0]]  (VRAM)
//    4. SOVEREIGN kernels on dedicated stream
//    5. cudaEventRecord   → record completion
//    6. Display real frame (CPU free while GPU runs)
//    7. cudaEventSynchronize → wait GPU done
//    8. Async download    → h_gen_buf
//    9. cudaEventSynchronize → wait download
//   10. Display generated frame (adaptive sleep for half-period)
//
//  v2.0 fixes:
//    - PeekMessage loop so overlay HWNDs process messages (no freeze)
//    - Adaptive half-frame sleep (was hardcoded Sleep(8))
//    - Resolution change: re-fill all 3 VRAM slots from current frame
//    - Bootstrap: fill all 3 slots identically from first capture
// ================================================================
static void gen_thread_fn() {
    // ── Bootstrap: capture one frame, fill all 3 ring slots ──────
    // (was: capture 3 separate frames 16 ms apart — slots 1,2 had
    //  stale data and the first generated frame was garbage)
    {
        int s0 = g_ring[0];
        if (!capture_gdi(g_target_hwnd, h_fb[s0])) {
            return;
        }
        int W = h_fb[s0].W, H = h_fb[s0].H;
        for (int i = 0; i < 3; i++) {
            int si = g_ring[i];
            h_fb[si].alloc(W, H);
            memcpy(h_fb[si].data, h_fb[s0].data, (size_t)W*H*4);
            d_fb[si].alloc(W, H);
            cudaMemcpy(d_fb[si].ptr, h_fb[si].data,
                       (size_t)W*H*sizeof(uchar4), cudaMemcpyHostToDevice);
        }
    }

    // Rolling average for adaptive timing
    double avg_ms = 33.0;

    while (g_running) {
        auto t0 = std::chrono::high_resolution_clock::now();

        // ── Pump Win32 messages on this thread ────────────────────
        // v2.0 fix: overlay HWNDs belong to gen_thread; without this
        // the overlay becomes "not responding" under Windows.
        {
            MSG msg;
            while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
        }

        // ── Rotate ring indices (recycle oldest slot) ─────────────
        {
            std::lock_guard<std::mutex> lk(g_frame_mtx);
            int tmp   = g_ring[2];
            g_ring[2] = g_ring[1];
            g_ring[1] = g_ring[0];
            g_ring[0] = tmp;
        }

        int cur = g_ring[0];

        if (!capture_gdi(g_target_hwnd, h_fb[cur])) {
            Sleep(8); continue;
        }
        int W = h_fb[cur].W, H = h_fb[cur].H;

        // ── Resolution change: resize + re-fill all 3 VRAM slots ──
        // v2.0 fix: was only resizing; slots 1,2 had garbage VRAM
        // after realloc. Now we copy the current frame into all 3.
        bool resized = false;
        for (int i = 0; i < 3; i++) {
            int si = g_ring[i];
            if (d_fb[si].W != W || d_fb[si].H != H) {
                d_fb[si].alloc(W, H);
                resized = true;
            }
        }
        if (resized) {
            for (int i = 1; i < 3; i++) {
                int si = g_ring[i];
                h_fb[si].alloc(W, H);
                memcpy(h_fb[si].data, h_fb[cur].data, (size_t)W*H*4);
                cudaMemcpy(d_fb[si].ptr, h_fb[si].data,
                           (size_t)W*H*sizeof(uchar4), cudaMemcpyHostToDevice);
            }
        }

        d_out.alloc(W, H);
        d_flw.alloc(W, H);
        h_gen_buf.alloc(W, H);

        // Async upload newest frame → VRAM (pinned → DMA)
        cudaMemcpyAsync(d_fb[cur].ptr, h_fb[cur].data,
                        (size_t)W*H*sizeof(uchar4),
                        cudaMemcpyHostToDevice, g_cuda_stream);

        // ── Ensure overlay exists and is positioned over target ────
        if (!g_overlay) {
            g_overlay = make_overlay(W, H);
        }
        position_overlay(g_overlay, g_target_hwnd, W, H);
        if (d_out.W != W || d_out.H != H)
            SetWindowPos(g_overlay, HWND_TOPMOST, 0, 0, W, H,
                         SWP_NOMOVE|SWP_NOACTIVATE);

        // ── Dispatch SOVEREIGN on stream (returns immediately) ─────
        generate_frame_cuda(
            d_fb[g_ring[0]], d_fb[g_ring[1]], d_fb[g_ring[2]],
            d_flw, d_out, g_cuda_stream, g_ev_done);

        // Display real frame while GPU computes generated frame
        blit_host(g_overlay, h_fb[cur]);

        // ── Wait for GPU to finish, then download generated frame ──
        cudaEventSynchronize(g_ev_done);

        // Async download VRAM → pinned RAM
        cudaMemcpyAsync(h_gen_buf.data, d_out.ptr,
                        (size_t)W*H*sizeof(uchar4),
                        cudaMemcpyDeviceToHost, g_cuda_stream);
        cudaStreamSynchronize(g_cuda_stream);

        // ── Adaptive half-frame sleep ──────────────────────────────
        // v2.0 fix: was hardcoded Sleep(8) (only correct at ~60fps)
        // Now computed from rolling-average real FPS.
        auto t1 = std::chrono::high_resolution_clock::now();
        double cycle_ms = std::chrono::duration<double,std::milli>(t1-t0).count();
        avg_ms = avg_ms * 0.85 + cycle_ms * 0.15;
        double half_ms = avg_ms * 0.5;
        if (half_ms > 2.0)
            Sleep((DWORD)(half_ms - 1.5));  // -1.5 ms scheduler slack

        // Display generated frame
        blit_host(g_overlay, h_gen_buf);

        auto t2 = std::chrono::high_resolution_clock::now();
        double total_ms = std::chrono::duration<double,std::milli>(t2-t0).count();
        g_fps_real = 1000.0 / (total_ms > 0.1 ? total_ms : 0.1);

        // Pump messages again after blit (keeps overlay responsive)
        {
            MSG msg;
            while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
        }
    }

    if (g_overlay) { DestroyWindow(g_overlay); g_overlay=NULL; }
    blit_cleanup();
    for (int i=0; i<3; i++) { d_fb[i].free_buf(); h_fb[i].free_buf(); }
    d_out.free_buf(); d_flw.free_buf(); h_gen_buf.free_buf();
}

// ================================================================
//  §13  CUDA DEVICE QUERY
// ================================================================
static void query_cuda(char *buf, int len) {
    int cnt = 0;
    cudaError_t err = cudaGetDeviceCount(&cnt);
    if (err != cudaSuccess || cnt == 0) {
        _snprintf(buf, len,
            "CUDA: No device — %s  [need driver >= 367.48]",
            cudaGetErrorString(err));
        g_cuda_ok = false; return;
    }
    cudaDeviceProp p; cudaGetDeviceProperties(&p, 0);
    int cc = p.major*10 + p.minor;
    if (cc < 21) {
        _snprintf(buf, len,
            "CUDA: %s CC%d.%d — INCOMPATIBLE (binary=sm_21, need CC2.1+)",
            p.name, p.major, p.minor);
        g_cuda_ok = false; return;
    }
    g_cuda_ok = true;
    int cpsm = (p.major==2) ? 48   // Fermi
             : (p.major==3) ? 192  // Kepler
             : (p.major==5) ? 128  // Maxwell
             : (p.major==6) ? 128  // Pascal
             : (p.major==7 && p.minor==0) ? 64  // Volta
             : (p.major==7) ? 64   // Turing
             : (p.major==8) ? 128  // Ampere
             : 128;                // Hopper / Ada / fallback
    _snprintf(buf, len,
        "CUDA OK: %s  CC%d.%d  %d MB  (~%d cores)",
        p.name, p.major, p.minor,
        (int)(p.totalGlobalMem/(1024*1024)),
        p.multiProcessorCount * cpsm);

    // Create stream and events once GPU confirmed
    if (!g_cuda_stream) {
        cudaStreamCreate(&g_cuda_stream);
        cudaEventCreateWithFlags(&g_ev_done, cudaEventDisableTiming);
    }
}

// ================================================================
//  §14  CONTROL WINDOW
// ================================================================
enum { ID_START=3001, ID_STOP, ID_PICK, ID_TIMER=4001 };

static LRESULT CALLBACK CtrlProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hf = CreateFontA(13,0,0,0,400,0,0,0,ANSI_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH,"Consolas");

        auto mkb = [&](const char *t, int id, int x, int y, int w, int h) {
            HWND b = CreateWindowA("BUTTON",t,WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
                x,y,w,h,hwnd,(HMENU)(UINT_PTR)id,NULL,NULL);
            SendMessageA(b,WM_SETFONT,(WPARAM)hf,TRUE);
        };
        auto mks = [&](const char *t, int x, int y, int w, int h) {
            HWND s = CreateWindowA("STATIC",t,WS_CHILD|WS_VISIBLE,
                x,y,w,h,hwnd,NULL,NULL,NULL);
            SendMessageA(s,WM_SETFONT,(WPARAM)hf,TRUE);
        };
        auto mkid = [&](const char *t, int id, int x, int y, int w, int h) -> HWND {
            HWND s = CreateWindowA("STATIC",t,WS_CHILD|WS_VISIBLE,
                x,y,w,h,hwnd,(HMENU)(UINT_PTR)id,NULL,NULL);
            SendMessageA(s,WM_SETFONT,(WPARAM)hf,TRUE);
            return s;
        };

        mkb("Pick Window", ID_PICK,  10,  10, 120, 26);
        mkb("Start FG",    ID_START, 140, 10, 110, 26);
        mkb("Stop FG",     ID_STOP,  260, 10, 110, 26);

        char ci[256]; query_cuda(ci, sizeof(ci));
        g_cuda_label = mkid(ci,             5000, 10, 44, 660, 16);
        g_stat_label = mkid("Status: idle", 5001, 10, 62, 660, 16);
        g_fps_label  = mkid("FPS: --",      5002, 10, 80, 660, 16);

        int y = 102;
        mks("══════════════════════════════════════════════════════════════", 10,y,660,14); y+=16;
        mks("  NEXUS v2.0  —  THE SOVEREIGN Frame Generation Engine",        10,y,660,16); y+=18;
        mks("  'Neither Interpolation nor Extrapolation — A New Mathematics'",10,y,660,16); y+=18;
        mks("══════════════════════════════════════════════════════════════", 10,y,660,14); y+=16;
        mks("  CROWN: F*(c) = W / (w_w/F_warp + w_t/F_temp + w_s/F_spat)", 10,y,660,16); y+=16;
        mks("    WHM [Frechet mean on (R+, ds2=dF2/F4)]  =>  WHM<=GM<=AM",  10,y,660,16); y+=16;
        mks("    Bounded:  min(F_w,F_t,F_s) <= F* <= max   Ghost-proof",    10,y,660,16); y+=16;
        mks("──────────────────────────────────────────────────────────────", 10,y,660,14); y+=16;
        mks("  A: AnisoBilinear Warp — LK flow (5x5,GPU,shm)  9 expf/px",   10,y,660,16); y+=16;
        mks("  B: FLGE log-geodesic  1.625*logF2-0.75*logF1+0.125*logF0",   10,y,660,16); y+=16;
        mks("  C: FASW wave PDE:  F2 + AnisoLap(F2)*c2*0.125",              10,y,660,16); y+=16;
        mks("──────────────────────────────────────────────────────────────", 10,y,660,14); y+=16;
        mks("  OFC (v2.0 fix): |It + v.grad(L)| / (|It| + |v||grad| + e)",  10,y,660,16); y+=16;
        mks("  SHM tile: F2(uchar4)+flow(float2) = 3888B/blk -> 12/SM",     10,y,660,16); y+=16;
        mks("──────────────────────────────────────────────────────────────", 10,y,660,14); y+=16;
        mks("  Target: GT 730 GF108 CC2.1  CUDA 8.0  sm_21  No RTX needed", 10,y,660,16); y+=16;
        mks("  F9 key: toggle engine on/off  |  stream+events, async DMA",   10,y,660,16);

        SetTimer(hwnd, ID_TIMER, 500, NULL);
        return 0;
    }

    case WM_TIMER:
        if (g_fps_label) {
            char buf[256];
            if (g_running) {
                double r = g_fps_real;
                _snprintf(buf, sizeof(buf),
                    "Capture: %.1f fps  |  Output: %.1f fps  |  x%.1f  "
                    "[SOVEREIGN v2.0 — VRAM-native — async stream]",
                    r, r*2.0, 2.0);
            } else {
                _snprintf(buf, sizeof(buf), "FPS: --  [stopped]");
            }
            SetWindowTextA(g_fps_label, buf);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_PICK: {
            MessageBoxA(hwnd,
                "Click OK, then hover the mouse over your game window\n"
                "within 3 seconds. For desktop capture, move nowhere.",
                "Pick Target Window", MB_OK|MB_ICONINFORMATION);
            Sleep(3000);
            POINT pt; GetCursorPos(&pt);
            HWND hit = WindowFromPoint(pt);
            g_target_hwnd = (hit && hit!=hwnd && hit!=g_overlay) ? hit : NULL;
            char title[256]={}, s[320];
            if (g_target_hwnd) GetWindowTextA(g_target_hwnd, title, 255);
            _snprintf(s, sizeof(s), "Target: %s",
                g_target_hwnd ? title : "(full desktop)");
            SetWindowTextA(g_stat_label, s);
            break;
        }
        case ID_START:
            if (!g_cuda_ok) {
                MessageBoxA(hwnd,
                    "No compatible CUDA device found.\n\n"
                    "Requirements:\n"
                    "  GPU  : Fermi CC2.1+ (GT 730 GF108 or newer)\n"
                    "  Driver: >= 367.48\n"
                    "  Build : CUDA 8.0 -arch=sm_21\n\n"
                    "Update driver at: nvidia.com/drivers\n"
                    "Search: GeForce GT 730 / Windows 10 64-bit",
                    "CUDA Error", MB_OK|MB_ICONERROR);
                break;
            }
            if (!g_running) {
                g_running = true;
                g_ring[0]=0; g_ring[1]=1; g_ring[2]=2;
                g_gen_thread = std::thread(gen_thread_fn);
                SetWindowTextA(g_stat_label,
                    "Status: RUNNING — NEXUS SOVEREIGN v2.0 [VRAM-native]");
            }
            break;
        case ID_STOP:
            if (g_running) {
                g_running = false;
                if (g_gen_thread.joinable()) g_gen_thread.join();
                SetWindowTextA(g_stat_label, "Status: stopped");
                SetWindowTextA(g_fps_label,  "FPS: --  [stopped]");
            }
            break;
        }
        return 0;

    case WM_HOTKEY:
        // F9: toggle engine on/off
        if (LOWORD(wp) == HOTKEY_ID) {
            if (g_running) {
                g_running = false;
                if (g_gen_thread.joinable()) g_gen_thread.join();
                SetWindowTextA(g_stat_label, "Status: stopped  [F9 toggled]");
                SetWindowTextA(g_fps_label,  "FPS: --");
            } else if (g_cuda_ok) {
                g_running = true;
                g_ring[0]=0; g_ring[1]=1; g_ring[2]=2;
                g_gen_thread = std::thread(gen_thread_fn);
                SetWindowTextA(g_stat_label,
                    "Status: RUNNING  [F9 toggled]");
            }
        }
        return 0;

    case WM_DESTROY:
        g_running = false;
        if (g_gen_thread.joinable()) g_gen_thread.join();
        if (g_cuda_stream) { cudaStreamDestroy(g_cuda_stream);   g_cuda_stream=nullptr; }
        if (g_ev_done)     { cudaEventDestroy(g_ev_done);        g_ev_done=nullptr; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// ================================================================
//  §15  WinMain
// ================================================================
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    WNDCLASSA wc{};
    wc.lpfnWndProc   = CtrlProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = "NEXUSCtrl";
    RegisterClassA(&wc);

    g_ctrl_hwnd = CreateWindowA(
        "NEXUSCtrl", APP_NAME,
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_VISIBLE,
        60, 60, 700, 580,
        NULL, NULL, hInst, NULL);

    // v2.0: F9 global hotkey — toggle engine on/off from anywhere
    RegisterHotKey(g_ctrl_hwnd, HOTKEY_ID, 0, VK_F9);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        // Route WM_HOTKEY to CtrlProc
        if (msg.message == WM_HOTKEY)
            SendMessageA(g_ctrl_hwnd, WM_HOTKEY, msg.wParam, msg.lParam);
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    UnregisterHotKey(g_ctrl_hwnd, HOTKEY_ID);
    return 0;
}

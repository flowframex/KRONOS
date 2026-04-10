/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║            K R O N O S   v 1 . 0  —  "The Eternal Frame"               ║
 * ║         Generalized Fréchet Frame Synthesis Engine                      ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  Evolved from:                                                           ║
 * ║    NEXUS v1.1      — "The SOVEREIGN"  (CUDA, LK Flow, WHM, FLGE, FASW) ║
 * ║    FlowFrameX v8.0 — "APEX"           (D3D11, DXGI, TCF, Gate5/6)      ║
 * ║                                                                          ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  NEW MATHEMATICS — GFF: Generalized Fréchet Fusion                      ║
 * ║  ─────────────────────────────────────────────────                      ║
 * ║  Replaces the fixed WHM (p=−1) with an adaptive power mean p∈[−1,0].   ║
 * ║                                                                          ║
 * ║    M_p(Fw, Ft, Fs) = exp( lerp(log F_GM, log F_HM, p_neg) )            ║
 * ║                                                                          ║
 * ║    p_neg = 0  →  Weighted Geometric Mean (WGM):                         ║
 * ║                  F* = Fw^ww · Ft^wt · Fs^ws                             ║
 * ║                  Color-preserving; optimal for stationary/flat regions  ║
 * ║    p_neg = 1  →  Weighted Harmonic Mean (WHM):                          ║
 * ║                  F* = 1/(ww/Fw + wt/Ft + ws/Fs)                        ║
 * ║                  Ghost-proof; optimal for fast motion and occlusions    ║
 * ║    p_neg ∈ (0,1) → Smooth interpolation in Riemannian log-space        ║
 * ║                                                                          ║
 * ║    Adaptive selector:                                                    ║
 * ║      p_neg = saturate( max(|flow|²/MAX²·2, 1−c_div) )                  ║
 * ║           → motion_intensity drives toward WHM                          ║
 * ║           → occlusion_risk   drives toward WHM                          ║
 * ║           → static/flat scenes stay near WGM                            ║
 * ║                                                                          ║
 * ║    Boundedness proof (inherited from NEXUS + extended):                 ║
 * ║      Both F_GM and F_HM are bounded by [min(Fw,Ft,Fs), max(Fw,Ft,Fs)]  ║
 * ║      Log-space lerp stays within those bounds (convexity of log space)  ║
 * ║      → IMPOSSIBLE to produce ghost or artifact outside known range      ║
 * ║      → WGM ≤ AM, WHM ≤ GM ≤ WGM ≤ AM  (all sub-arithmetic, safe)      ║
 * ║                                                                          ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  THREE PILLARS (from NEXUS, corrected and ported to HLSL):              ║
 * ║                                                                          ║
 * ║  A — AnisoBilinear Backward Warp (Lucas-Kanade flow, HLSL Pass 1)       ║
 * ║      True 2D LK 5×5 structure tensor, closed-form 2×2 solve            ║
 * ║      Harris corner quality gate · Divergence confidence gate            ║
 * ║      Anisotropic 3×3 kernel: near-delta along motion, Gaussian ⊥       ║
 * ║      Shared-mem tile loading: ~5× fewer global reads on DDR3 bus        ║
 * ║                                                                          ║
 * ║  B — FLGE Log-Geodesic Temporal  (in LINEAR LIGHT — NEXUS bug fixed)   ║
 * ║      log F* = 1.625·log F2 − 0.75·log F1 + 0.125·log F0               ║
 * ║      Coefficients sum to 1 (partition of unity)                         ║
 * ║      Ratio-preserving, zero colour blur, LRA amplitude clamp            ║
 * ║      CORRECTION: NEXUS applied this in gamma space (error).             ║
 * ║      KRONOS applies it in linearised light (IEC 61966-2-1).             ║
 * ║                                                                          ║
 * ║  C — FASW Anisotropic Spatial Wave PDE  (in LINEAR LIGHT)               ║
 * ║      F_spat = F2 + AnisoLap(F2)·c²·τ²/2                               ║
 * ║      Edge-adaptive wave speed: c² = C0²/(1+λG) — slower at edges       ║
 * ║      Anisotropy tensor: Dxx=1−α·Lx²/G, Dyy=1−α·Ly²/G                 ║
 * ║                                                                          ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  SIX GATES:                                                              ║
 * ║    G1 — JOD:       Jerk Occlusion Detection    (NEXUS)                  ║
 * ║    G2 — OFC:       Optical Flow Consistency    (NEXUS)                  ║
 * ║    G3 — DIV:       Divergence Confidence Gate  (NEXUS)                  ║
 * ║    G4 — APEX TCF:  Tile-Consensus Flow         (FlowFrameX v8)          ║
 * ║    G5 — TCONSIST:  Temporal Consistency        (FlowFrameX v7)          ║
 * ║    G6 — SHARP:     In-register Sharpening      (FlowFrameX v8)          ║
 * ║                                                                          ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  PIPELINE (from FlowFrameX — full GPU-native, zero PCIe round-trips):  ║
 * ║    DXGI Desktop Duplication → CopyResource (GPU-native, zero CPU copy) ║
 * ║    CS Pass 1: LK Flow          (16×16 threadgroups, shared-mem tiles)   ║
 * ║    CS Pass 2: SOVEREIGN+GFF    (32×8  threadgroups, APEX TCF 3 KB GS)  ║
 * ║    D3D11 SwapChain Present()   (vsync-capable, zero GDI overlay)        ║
 * ║    Microsecond spin-wait · EMA capture rate · HIGH_PRIORITY_CLASS       ║
 * ║                                                                          ║
 * ║  Target: GT 730 GF108 Fermi (DDR3 64-bit bus)  i3-2120  6 GB DDR3      ║
 * ║  API:    D3D11 CS 5.0 (sm_5_0)  DXGI 1.2  Windows 8+                   ║
 * ║  Build:  cl /O2 /EHsc KRONOS_v1.cpp                                     ║
 * ║          (link: d3d11 dxgi d3dcompiler winmm)                           ║
 * ║                                                                          ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <mmsystem.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <thread>
#include <chrono>
#include <immintrin.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "winmm.lib")

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::high_resolution_clock;
using Dur   = std::chrono::duration<double, std::milli>;

/* ============================================================================
   Console color macros
   ============================================================================ */
#define A_RESET       "\033[0m"
#define A_BOLD        "\033[1m"
#define A_DIM         "\033[2m"
#define A_HIDE_CURSOR "\033[?25l"
#define A_SHOW_CURSOR "\033[?25h"
#define A_CLEAR       "\033[2J\033[H"

#define C_GOLD1  "\033[38;2;255;215;0m"
#define C_GOLD2  "\033[38;2;255;185;0m"
#define C_GOLD3  "\033[38;2;220;140;0m"
#define C_CYAN   "\033[38;2;0;230;255m"
#define C_TEAL   "\033[38;2;0;180;180m"
#define C_GREEN  "\033[38;2;80;255;120m"
#define C_RED    "\033[38;2;255;80;80m"
#define C_WHITE  "\033[38;2;240;240;240m"
#define C_GREY   "\033[38;2;100;100;120m"
#define C_LGREY  "\033[38;2;160;160;180m"
#define C_VIOLET "\033[38;2;180;100;255m"
#define C_PINK   "\033[38;2;255;120;200m"
#define B_ROW    "\033[48;2;18;18;30m"

static void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static void SleepUs(int us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }

static void HRule(int w, const char* c = C_GREY)
{
    printf("  %s", c);
    for (int i = 0; i < w; ++i) printf("\xe2\x94\x80");
    printf("%s\n", A_RESET); fflush(stdout);
}

static void TypeWrite(const char* text, int delayUs = 14000, const char* color = C_WHITE)
{
    printf("%s", color);
    for (const char* p = text; *p; ++p) { putchar(*p); fflush(stdout); SleepUs(delayUs); }
    printf(A_RESET); fflush(stdout);
}

static void AnimBar(int width, int durationMs, const char* label)
{
    int stepMs = durationMs / width;
    printf("\n  %s%s  %s", C_TEAL, label, A_RESET);
    printf("  %s[%s", C_GREY, A_RESET);
    for (int i = 0; i < width; ++i) {
        int r = 180 - (int)(80.0 * i / width);
        int g = 80  + (int)(100.0 * i / width);
        int b = 255;
        printf("\033[38;2;%d;%d;%dm\xe2\x96\x88%s", r, g, b, A_RESET);
        fflush(stdout); SleepMs(stepMs);
    }
    printf("%s]%s\n", C_GREY, A_RESET); fflush(stdout);
}

static void InitConsole()
{
    SetConsoleOutputCP(65001); SetConsoleCP(65001);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0; GetConsoleMode(hOut, &mode);
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
        COORD s = {95, 600}; SetConsoleScreenBufferSize(hOut, s);
        SMALL_RECT r = {0, 0, 94, 38}; SetConsoleWindowInfo(hOut, TRUE, &r);
    }
    SetConsoleTitleA("KRONOS v1.0  |  Eternal Frame Engine  [GFF + LK-SOVEREIGN + APEX]");
}

static void ShowSplash()
{
    printf(A_CLEAR A_HIDE_CURSOR "\n\n");

    // Title — gradient violet→gold
    const char* title = "  K  R  O  N  O  S";
    int tlen = (int)strlen(title);
    for (int i = 0; i < tlen; ++i) {
        float t = (float)i / tlen;
        int r = (int)(180 + 75*t);
        int g = (int)(100*t);
        int b = (int)(255*(1.0f - t));
        printf("\033[38;2;%d;%d;%dm\033[1m%c" A_RESET, r, g, b, title[i]);
        fflush(stdout); SleepMs(22);
    }
    printf("\n");
    printf("  %s%s  v 1 . 0   —   T h e   E t e r n a l   F r a m e%s\n\n", C_VIOLET, A_DIM, A_RESET);
    SleepMs(80);

    printf("  %s%sGeneralized Fréchet Synthesis  ·  LK Flow  ·  APEX TCF  ·  GFF Fusion%s\n",
           C_GREY, A_DIM, A_RESET);
    printf("  %s%sEvolved from NEXUS v1.1 × FlowFrameX v8.0  ·  GT 730 / i3-2120 Target%s\n\n",
           C_GREY, A_DIM, A_RESET);
    SleepMs(80);
    HRule(72, C_GOLD3);
    SleepMs(60);

    struct { const char* step; const char* detail; } steps[] = {
        {"D3D11 device + DXGI Desktop Dup      ", "GPU-native capture, zero PCIe round-trip" },
        {"LK Flow CS  (5×5 shm-tile, HLSL)     ", "True 2D vectors, Harris quality, c_div"  },
        {"SOVEREIGN CS  (FLGE+FASW+AnisoWarp)  ", "All in linear light (IEC 61966-2-1)"     },
        {"APEX TCF  (3 KB groupshared, 8 bars) ", "Flat/pan region fix via tile consensus"  },
        {"GFF Fusion  (adaptive p∈[−1,0])      ", "WGM↔WHM: color-safe ↔ ghost-proof"      },
        {"Gates 1-6  (JOD·OFC·DIV·TCF·TC·SHP) ", "Full confidence routing + sharpening"    },
        {"Swap-chain Present + µs spin-wait     ", "Microsecond frame pacing, EMA timing"    },
        {"HIGH_PRIORITY_CLASS + THREAD_HIGHEST  ", "Real-time scheduling active"             },
    };
    SleepMs(40);
    for (auto& s : steps) {
        TypeWrite("  > ", 5000, C_LGREY);
        TypeWrite(s.step, 4000, C_WHITE);
        printf("%s  %s%s%s\n", C_GREEN, A_DIM, s.detail, A_RESET);
        fflush(stdout); SleepMs(30);
    }

    AnimBar(42, 700, "KRONOS ENGINE READY");
    SleepMs(80);
    HRule(72, C_GOLD3);
    printf("\n"); fflush(stdout);
}

struct EngineConfig { int fps; };

static int ShowFpsMenu()
{
    printf("  %s%s  SELECT OUTPUT FPS%s\n\n", C_GOLD1, A_BOLD, A_RESET);
    struct { int key; int out; const char* note; } opts[] = {
        {1, 30,  ""},
        {2, 60,  "  recommended (GT 730 safe)"},
        {3, 90,  ""},
        {4, 120, "  max on GT 730"},
        {5, 144, ""},
        {6, 240, "  max performance"},
        {0, 0,   "  custom (1-360)"},
    };
    for (int i = 0; i < 7; ++i) {
        bool sp = (opts[i].key == 2 || opts[i].key == 4 || opts[i].key == 0);
        if (sp) printf(B_ROW);
        printf("  %s[%s%s%d%s%s]%s  %s%3d fps%s  %s%s%s\n",
               C_GREY, A_RESET, sp ? C_GOLD2 A_BOLD : C_WHITE,
               opts[i].key, A_RESET, C_GREY, A_RESET,
               sp ? C_GOLD1 A_BOLD : C_WHITE, opts[i].out, A_RESET,
               C_GREEN A_DIM, opts[i].note, A_RESET);
        if (sp) printf(A_RESET);
        fflush(stdout); SleepMs(25);
    }
    printf("\n"); HRule(60);
    printf("\n  %s->%s  Choice: %s", C_GOLD1, A_RESET, C_WHITE);
    fflush(stdout); printf(A_SHOW_CURSOR);
    int fps = 60, ch = getchar();
    printf(A_HIDE_CURSOR A_RESET);
    switch (ch) {
        case '1': fps = 30;  break;
        case '2': fps = 60;  break;
        case '3': fps = 90;  break;
        case '4': fps = 120; break;
        case '5': fps = 144; break;
        case '6': fps = 240; break;
        case '0': {
            printf("\n\n  %scustom (1-360)%s: %s", C_GREY, A_RESET, C_WHITE);
            fflush(stdout); printf(A_SHOW_CURSOR);
            if (scanf("%d", &fps) != 1) fps = 60;
            { int c; while ((c = getchar()) != '\n' && c != EOF) {} }
            printf(A_HIDE_CURSOR A_RESET);
            fps = std::max(1, std::min(360, fps));
            break;
        }
        default: fps = 60; break;
    }
    return fps;
}

static EngineConfig ShowMenu() { return EngineConfig{ShowFpsMenu()}; }

static void ShowRunning(int fps)
{
    printf(A_CLEAR "\n");
    printf("  %s%s  KRONOS v1.0  —  RUNNING%s\n\n", C_GOLD1, A_BOLD, A_RESET);
    HRule(68, C_GOLD3);
    auto Row = [](const char* icon, const char* lbl, const char* val, const char* vc = C_GREEN) {
        printf("  %s%s%s  %s%-26s%s  %s%s%s%s\n",
               C_TEAL, icon, A_RESET, C_LGREY, lbl, A_RESET, vc, A_BOLD, val, A_RESET);
        fflush(stdout);
    };
    char b[64];
    snprintf(b, sizeof(b), "%d fps out  (capture ~%d fps)", fps, fps/2);
    Row("\xe2\x9a\xa1", "Target output",      b, C_GOLD2);
    Row("\xf0\x9f\x94\xae", "Algorithm",      "KRONOS GFF — WGM\xe2\x86\x94WHM adaptive fusion", C_VIOLET);
    Row("\xf0\x9f\x8c\x8a", "Optical flow",   "Lucas-Kanade 5x5 true 2D + APEX TCF", C_CYAN);
    Row("\xe2\x9c\xa8", "Pillars",             "AnisoWarp + FLGE(linear) + FASW(linear)", C_CYAN);
    Row("\xf0\x9f\x94\x92", "Capture",         "DXGI Desktop Duplication (zero PCIe)", C_GREEN);
    Row("\xe2\x8f\xb1", "Timing",              "µs spin-wait + EMA + timeBeginPeriod(1)", C_GREEN);
    Row("\xe2\x8c\xa8", "Stop hotkey",         "Ctrl+Shift+X", C_RED);
    printf("\n"); HRule(68, C_GOLD3); printf("\n");
    printf("  %s%s  Waiting for first frame...%s\n\n", C_GREY, A_DIM, A_RESET);
    fflush(stdout);
}

static double g_fpsOut = 0, g_fpsCap = 0;
static void PrintStats(double capFps, double outFps, double captureMs, bool idle)
{
    printf("\033[3A");
    printf("  %s%-28s%s  %s%.1f%s/%s%.1f%s fps  cap %.1f ms  %s%s%s\n",
           C_TEAL, "capture / output fps", A_RESET,
           C_GOLD1 A_BOLD, capFps, A_RESET, C_GOLD2, outFps, A_RESET,
           captureMs,
           idle ? C_GREY : C_GREEN, idle ? "IDLE" : "ACTIVE", A_RESET);
    printf("  %sMultiplier%s  %s%.2fx%s  %s%s%s\n",
           C_LGREY, A_RESET, C_VIOLET A_BOLD, outFps / std::max(capFps, 0.1), A_RESET,
           C_GREY, "(press Ctrl+Shift+X to stop)", A_RESET);
    printf("  %s[KRONOS] LK + GFF + APEX active%s\n\n", C_CYAN A_DIM, A_RESET);
    fflush(stdout);
}

/* ============================================================================
   HLSL PASS 1  —  Lucas-Kanade Optical Flow
   ============================================================================
   True 2D LK per pixel from 5×5 structure tensor.
   Closed-form 2×2 solve.  Harris corner quality.  Shared-mem tile loading.

   Tile dims: LK_TW=22, LK_TH=22  (block 16×16, PAD=3)
   Shared memory: 2 tiles × 484 floats × 4B = 3872 B  (Fermi: 48 KB/SM → ~12 blocks/SM)
   ~5× fewer global memory transactions vs direct global loads on 64-bit DDR3 bus.

   Output:
     tFlow  (R32G32_FLOAT)  per-pixel (vx, vy)
     tQual  (R32_FLOAT)     Harris corner quality [0, 1]
   ============================================================================ */
static const char* kLKFlowCS = R"HLSL(
// Lucas-Kanade 5x5 Optical Flow — HLSL Compute Shader
// KRONOS Pass 1: true 2D motion vectors with shared-memory tile loading

#define LK_R      2
#define LK_PAD    3       // LK_R + 1
#define BLOCK_W   16
#define BLOCK_H   16
#define LK_TW     22      // BLOCK_W + 2*LK_PAD
#define LK_TH     22      // BLOCK_H + 2*LK_PAD
#define MAX_FLOW  80.0
#define LK_HSCALE 8000.0  // Harris response scale to [0,1]

Texture2D<float4>    tF2    : register(t0);   // current frame  (BGRA)
Texture2D<float4>    tF1    : register(t1);   // previous frame (BGRA)
RWTexture2D<float2>  tFlow  : register(u0);   // output (vx, vy)
RWTexture2D<float>   tQual  : register(u1);   // output Harris quality

cbuffer LKCB : register(b0) { uint lkW; uint lkH; uint2 lkPad; }

groupshared float sL2[LK_TH][LK_TW];
groupshared float sL1[LK_TH][LK_TW];

// Luminance — note: input BGRA, so .rgb = (B,G,R) in HLSL
float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

[numthreads(BLOCK_W, BLOCK_H, 1)]
void CSMain(uint3 gid : SV_GroupID,
            uint3 tid : SV_GroupThreadID,
            uint3 did : SV_DispatchThreadID)
{
    const int bx = (int)(gid.x * BLOCK_W);
    const int by = (int)(gid.y * BLOCK_H);
    const int threadLinear = tid.y * BLOCK_W + tid.x;
    const int tileTotal    = LK_TW * LK_TH;  // 484

    // --- Cooperative shared-memory tile load (all 256 threads) ----------
    // Each thread loads ceil(484/256)=2 cells max.
    [loop]
    for (int i = threadLinear; i < tileTotal; i += BLOCK_W * BLOCK_H) {
        int lx = i % LK_TW;
        int ly = i / LK_TW;
        int gx = clamp(bx - LK_PAD + lx, 0, (int)lkW - 1);
        int gy = clamp(by - LK_PAD + ly, 0, (int)lkH - 1);
        sL2[ly][lx] = luma(tF2.Load(int3(gx, gy, 0)).rgb);
        sL1[ly][lx] = luma(tF1.Load(int3(gx, gy, 0)).rgb);
    }
    GroupMemoryBarrierWithGroupSync();

    const int x = (int)did.x;
    const int y = (int)did.y;
    if (x >= (int)lkW || y >= (int)lkH) return;

    // --- Accumulate 5x5 structure tensor from shared memory ---------------
    float sxx = 0, syy = 0, sxy = 0, sxt = 0, syt = 0;

    [unroll]
    for (int dy = -LK_R; dy <= LK_R; dy++) {
        [unroll]
        for (int dx = -LK_R; dx <= LK_R; dx++) {
            int lx = tid.x + LK_PAD + dx;
            int ly = tid.y + LK_PAD + dy;

            // Central-difference spatial gradient of F2 from shared mem
            float Ix = (sL2[ly][lx + 1] - sL2[ly][lx - 1]) * 0.5;
            float Iy = (sL2[ly + 1][lx] - sL2[ly - 1][lx]) * 0.5;
            float It =  sL2[ly][lx] - sL1[ly][lx];

            sxx += Ix * Ix;
            syy += Iy * Iy;
            sxy += Ix * Iy;
            sxt += Ix * It;
            syt += Iy * It;
        }
    }

    // --- Closed-form 2x2 system solve  [sxx sxy; sxy syy][vx;vy]=[-sxt;-syt] --
    float det = sxx * syy - sxy * sxy;
    float tr  = sxx + syy + 1e-7;

    // Harris corner quality q = det/trace^2
    float harris = saturate((det / (tr * tr)) * LK_HSCALE);

    float2 flow = float2(0, 0);
    if (abs(det) > 1e-8) {
        flow.x = -(syy * sxt - sxy * syt) / det;
        flow.y = -(sxx * syt - sxy * sxt) / det;
        // Clamp to physical max (prevents outlier domination in APEX TCF)
        float flenSq = dot(flow, flow);
        if (flenSq > MAX_FLOW * MAX_FLOW)
            flow *= MAX_FLOW * rsqrt(flenSq);
    }

    tFlow[int2(x, y)] = flow;
    tQual[int2(x, y)] = harris;
}
)HLSL";

/* ============================================================================
   HLSL PASS 2  —  KRONOS SOVEREIGN SYNTHESIS
   ============================================================================
   PILLAR A: AnisoBilinear backward warp (LK flow from Pass 1)
   PILLAR B: FLGE log-geodesic temporal in LINEAR LIGHT
   PILLAR C: FASW anisotropic wave PDE in LINEAR LIGHT
   GFF:      Generalized Fréchet Fusion (adaptive power mean p in [-1,0])
   GATES:    JOD · OFC · DIV · APEX-TCF · TConsist · Sharpening
   ============================================================================ */
static const char* kSovereignCS = R"HLSL(
// KRONOS SOVEREIGN Synthesis Shader — HLSL Compute Shader
// Combines NEXUS mathematics + FlowFrameX engineering + GFF new formula

Texture2D<float4>    tF0    : register(t0);   // F0  frame N-2  (BGRA)
Texture2D<float4>    tF1    : register(t1);   // F1  frame N-1  (BGRA)
Texture2D<float4>    tF2    : register(t2);   // F2  frame N    (BGRA)
Texture2D<float2>    tFlowT : register(t3);   // LK  (vx, vy)
Texture2D<float>     tQualT : register(t4);   // LK  Harris quality
RWTexture2D<float4>  tOut   : register(u0);   // prediction (RGBA out)
SamplerState         gBilin : register(s0);   // bilinear clamp

cbuffer SovCB : register(b0)
{
    float captureMs;  // live EMA capture interval (ms)
    float tau;        // temporal fraction: halfMs / captureMs
    float sovW;       // frame width
    float sovH;       // frame height
}

// APEX TCF groupshared — 3×256×4 = 3072 bytes (Fermi: 48 KB/SM, fine)
groupshared float gs_fx[256];
groupshared float gs_fy[256];
groupshared float gs_fw[256];

// ---- sRGB (IEC 61966-2-1, pow(2.4)) -------------------------------------
float lin1(float c) { return (c <= 0.04045) ? c / 12.92 : pow(max((c + 0.055) / 1.055, 0.0), 2.4); }
float gam1(float c) { c = max(c, 0.0); return (c <= 0.0031308) ? c * 12.92 : 1.055 * pow(c, 1.0/2.4) - 0.055; }
float3 sLin(float3 c) { return float3(lin1(c.r), lin1(c.g), lin1(c.b)); }
float3 sGam(float3 c) { return float3(gam1(c.r), gam1(c.g), gam1(c.b)); }

// Luma (BGRA-native: .r=B, .g=G, .b=R)
float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

// ---- Anisotropic backward warp (NEXUS Pillar A, adapted to HLSL) --------
// 3x3 kernel: near-delta along motion, Gaussian perpendicular
// Falls back to bilinear for slow pixels (|v| < 0.3 px)
float3 AnisoBilinear(float2 uv_center, float2 motion)
{
    float2 invD = 1.0 / float2(sovW, sovH);
    float vlen = length(motion);
    if (vlen < 0.3)
        return tF2.SampleLevel(gBilin, uv_center, 0).rgb;

    float2 mh = motion / vlen;               // unit along motion
    float2 mp = float2(-mh.y, mh.x);         // perpendicular unit

    // ANISO_EPS=0.05 (near-delta along motion), ANISO_SIGMA=0.5 (Gaussian perp)
    const float inv_ee = 1.0 / (0.05 * 0.05);
    const float inv_ss = 1.0 / (0.50 * 0.50);

    float3 sum = 0; float wsum = 0;
    [unroll] for (int dy = -1; dy <= 1; dy++) {
        [unroll] for (int dx = -1; dx <= 1; dx++) {
            float pm = dx * mh.x + dy * mh.y;
            float pp = dx * mp.x + dy * mp.y;
            float w = exp(-pm*pm * inv_ee) * exp(-pp*pp * inv_ss);
            sum += tF2.SampleLevel(gBilin, uv_center + float2(dx,dy) * invD, 0).rgb * w;
            wsum += w;
        }
    }
    return sum / max(wsum, 1e-7);
}

[numthreads(32, 8, 1)]
void CSMain(uint3 did : SV_DispatchThreadID, uint groupIdx : SV_GroupIndex)
{
    uint W = (uint)sovW, H = (uint)sovH;
    bool oob = (did.x >= W || did.y >= H);
    int2 pos = int2(min(did.x, W-1u), min(did.y, H-1u));

    // ---- Centre pixel loads (BGRA native, 3 reads) -----------------------
    float3 c2s = tF2.Load(int3(pos, 0)).rgb;
    float3 c1s = tF1.Load(int3(pos, 0)).rgb;
    float3 c0s = tF0.Load(int3(pos, 0)).rgb;

    // ---- Static pixel detection (skip full synthesis, participate in TCF) -
    float earlyIt  = abs(luma(c2s) - luma(c1s));
    float earlyVel = luma(abs(c2s - c1s));
    bool  isStatic = oob || (earlyIt < 0.004 && earlyVel < 0.004);

    // ---- LK flow + quality load ------------------------------------------
    float2 localFlow = tFlowT.Load(int3(pos, 0));
    float  lk_q      = tQualT.Load(int3(pos, 0));

    // ---- Compute per-pixel QLFE confidence for APEX TCF ------------------
    // (Used purely as APEX TCF input weight. Flat/textureless pixels → 0)
    float my_w_nf = 0.0;
    float my_lumaC = 0.0, my_lumaE = 0.0, my_lumaW = 0.0;
    float my_lumaN = 0.0, my_lumaS = 0.0;

    int2 nb_e = int2(min(pos.x+1,(int)W-1), pos.y);
    int2 nb_w = int2(max(pos.x-1,0), pos.y);
    int2 nb_n = int2(pos.x, max(pos.y-1,0));
    int2 nb_s = int2(pos.x, min(pos.y+1,(int)H-1));

    if (!isStatic) {
        float3 c2E = tF2.Load(int3(nb_e,0)).rgb;
        float3 c2W = tF2.Load(int3(nb_w,0)).rgb;
        float3 c2N = tF2.Load(int3(nb_n,0)).rgb;
        float3 c2S = tF2.Load(int3(nb_s,0)).rgb;
        my_lumaC = luma(c2s);
        my_lumaE = luma(c2E);
        my_lumaW = luma(c2W);
        my_lumaN = luma(c2N);
        my_lumaS = luma(c2S);
        float Ix      = 0.5 * (my_lumaE - my_lumaW);
        float Iy      = 0.5 * (my_lumaS - my_lumaN);
        float gradSq  = Ix*Ix + Iy*Iy;
        float F_max   = 0.25 * sovW;
        float flowSpd = dot(localFlow, localFlow);
        float w_flat  = saturate(gradSq * 5000.0);
        float w_outl  = exp(-flowSpd / (F_max*F_max + 1e-4));
        my_w_nf = w_flat * w_outl * lk_q;  // LK quality as third gate
    }

    // ---- APEX TCF: write to groupshared (all threads, incl. static/OOB) --
    gs_fx[groupIdx] = isStatic ? 0.0 : localFlow.x * my_w_nf;
    gs_fy[groupIdx] = isStatic ? 0.0 : localFlow.y * my_w_nf;
    gs_fw[groupIdx] = isStatic ? 0.0 : my_w_nf;

    // ---- 8-stage binary tree reduction (ALL threads participate) ----------
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint stride = 128; stride >= 1; stride >>= 1) {
        if (groupIdx < stride) {
            gs_fx[groupIdx] += gs_fx[groupIdx + stride];
            gs_fy[groupIdx] += gs_fy[groupIdx + stride];
            gs_fw[groupIdx] += gs_fw[groupIdx + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    float  tileW    = gs_fw[0];
    float2 tileFlow = (tileW > 1e-6) ? float2(gs_fx[0]/tileW, gs_fy[0]/tileW) : float2(0,0);
    float  tileConf = saturate(tileW / 12.0);   // 12 confident edge pixels → full tile trust

    // ---- Early exit for OOB / static pixels (after all barriers) --------
    if (oob) return;
    if (isStatic) {
        // Static: no motion — output current frame, converted BGR→RGB for RGBA target
        tOut[pos] = float4(c2s.bgr, 1.0);
        return;
    }

    // ---- APEX TCF: blend local flow with tile consensus ------------------
    float  tileBlend = saturate(tileConf * (1.0 - my_w_nf));
    float2 finalFlow = lerp(localFlow, tileFlow, tileBlend);
    float  finalConf = saturate(my_w_nf + tileConf*(1.0-my_w_nf)*0.6);

    // ---- Divergence confidence (NEXUS G3: div>0=disocclusion, <0=occlusion) --
    int2 xl = int2(max(pos.x-1, 0), pos.y),          xr = int2(min(pos.x+1,(int)W-1), pos.y);
    int2 yu = int2(pos.x, max(pos.y-1, 0)),           yd = int2(pos.x, min(pos.y+1,(int)H-1));
    float dvx = (tFlowT.Load(int3(xr,0)).x - tFlowT.Load(int3(xl,0)).x) * 0.5;
    float dvy = (tFlowT.Load(int3(yd,0)).y - tFlowT.Load(int3(yu,0)).y) * 0.5;
    float divV  = dvx + dvy;
    float c_div = exp(-divV*divV * (1.0/(0.40*0.40)));  // SIGMA_DIV=0.40
    float w_w   = saturate(c_div * lk_q * (1.0 + tileConf*(1.0 - c_div*lk_q)));

    // ---- Load 4-neighbours for F2 (for FASW, JOD, OFC, G5, G6) ---------
    float3 c2E = tF2.Load(int3(nb_e,0)).rgb;
    float3 c2W = tF2.Load(int3(nb_w,0)).rgb;
    float3 c2N = tF2.Load(int3(nb_n,0)).rgb;
    float3 c2S = tF2.Load(int3(nb_s,0)).rgb;

    // ---- Convert to LINEAR LIGHT (IEC 61966-2-1) -------------------------
    float3 c2l  = sLin(c2s);
    float3 c1l  = sLin(c1s);
    float3 c0l  = sLin(c0s);
    float3 c2El = sLin(c2E);
    float3 c2Wl = sLin(c2W);
    float3 c2Nl = sLin(c2N);
    float3 c2Sl = sLin(c2S);

    // ---- Velocity and acceleration in linear space -----------------------
    float3 vel = c2l - c1l;
    float3 acc = c2l - 2.0*c1l + c0l;
    float  VV  = dot(vel, vel) + 1e-7;
    float  AA  = dot(acc, acc);

    // ---- G1 — JOD: Jerk Occlusion Detection  (NEXUS) --------------------
    float relJ   = AA / VV;
    float w_jerk = saturate(0.80 * relJ);

    // ---- Luminance gradient of F2 in linear space (for FASW + G2/h) -----
    float lumaC_l = luma(c2l);
    float Lx      = (luma(c2El) - luma(c2Wl)) * 0.5;
    float Ly      = (luma(c2Sl) - luma(c2Nl)) * 0.5;
    float G       = Lx*Lx + Ly*Ly + 1e-5;
    float h       = saturate(G * 0.0008);   // texture measure: 0=flat, 1=textured

    // ---- G2 — OFC: Optical Flow Consistency  (NEXUS) --------------------
    float V_lum  = luma(abs(vel));
    float VVl    = V_lum * V_lum;
    float VGmag  = sqrt(VVl + 1e-7) * sqrt(G);
    float cos_th = saturate(VVl / (VGmag + 1e-7));
    float ofc    = 0.20 * (1.0 - cos_th) * saturate(G / (G + 1e-4));

    // ---- Final confidence weights ----------------------------------------
    float w_t     = (1.0 - w_jerk) * h;
    float w_s     = saturate(w_jerk + 0.30*(1.0 - h) + ofc);
    float W_total = w_w + w_t + w_s + 1e-7;
    // Normalised weights (sum = 1)
    float nw = w_w / W_total, nt = w_t / W_total, ns = w_s / W_total;

    // ---- G5 — Temporal Consistency  (FlowFrameX) ------------------------
    float It      = luma(c2s) - luma(c1s);
    float It_prev = luma(c1s) - luma(c0s);
    float tConsist = (It * It_prev >= 0.0) ? 1.0 : 0.3;

    // ====================================================================
    //  PILLAR A — Anisotropic Backward Warp  (NEXUS A, HLSL)
    //  Backward-warp F2 by finalFlow*tau → synthesise F(t+tau)
    // ====================================================================
    float2 warpUV = (float2(pos) - finalFlow * tau + 0.5) / float2(sovW, sovH);
    float3 Fw_s   = AnisoBilinear(warpUV, finalFlow);   // sRGB (BGRA native)
    float3 Fw_l   = sLin(Fw_s);                          // → linear

    // ====================================================================
    //  PILLAR B — FLGE Log-Geodesic Temporal  (NEXUS B, corrected to LINEAR)
    //  KRONOS FIX: NEXUS applied FLGE in gamma space — wrong.
    //              KRONOS applies FLGE in linearised light.
    //  log F* = 1.625·log F2 − 0.75·log F1 + 0.125·log F0
    // ====================================================================
    const float LOG_FL  = 1e-4;
    const float RATIO_CAP = 3.0;
    float3 Ft_l;
    Ft_l.r = exp( 1.625*log(max(c2l.r,LOG_FL)) - 0.75*log(max(c1l.r,LOG_FL)) + 0.125*log(max(c0l.r,LOG_FL)) );
    Ft_l.g = exp( 1.625*log(max(c2l.g,LOG_FL)) - 0.75*log(max(c1l.g,LOG_FL)) + 0.125*log(max(c0l.g,LOG_FL)) );
    Ft_l.b = exp( 1.625*log(max(c2l.b,LOG_FL)) - 0.75*log(max(c1l.b,LOG_FL)) + 0.125*log(max(c0l.b,LOG_FL)) );
    // LRA: Log-Riemannian Amplitude Clamp (prevents bright blow-up at dark→bright)
    Ft_l = min(Ft_l, c2l * RATIO_CAP + LOG_FL);
    Ft_l = saturate(Ft_l);

    // ====================================================================
    //  PILLAR C — FASW Anisotropic Spatial Wave PDE  (NEXUS C, in LINEAR)
    //  F_spat = F2 + AnisoLap(F2) · c² · τ²/2
    //  Dxx = 1−α·Lx²/G,  Dyy = 1−α·Ly²/G  (edge-preserving diffusion)
    //  c² = C0²/(1+λG)  (wave slower at edges)
    // ====================================================================
    float Dxx  = 1.0 - 0.85*(Lx*Lx)/G;
    float Dyy  = 1.0 - 0.85*(Ly*Ly)/G;
    float cSq  = 4.0 / (1.0 + 5000.0*G);
    float coeff = cSq * 0.125;   // τ²/2 = 0.125 for τ=0.5
    float3 aLap = (c2El + c2Wl - 2.0*c2l)*Dxx + (c2Nl + c2Sl - 2.0*c2l)*Dyy;
    float3 Fs_l = saturate(c2l + aLap * coeff);

    // ====================================================================
    //  GFF — GENERALIZED FRÉCHET FUSION  (KRONOS NEW MATHEMATICS)
    //
    //  Adaptive power mean p ∈ [−1, 0]:
    //    p_neg → 0  (static/flat):  Weighted Geometric Mean (WGM)
    //                               F* = Fw^nw · Ft^nt · Fs^ns
    //                               Ideal when all estimates agree
    //    p_neg → 1  (motion/occ):  Weighted Harmonic Mean (WHM)
    //                               F* = 1/(nw/Fw + nt/Ft + ns/Fs)
    //                               Ghost-proof: spurious bright→ large 1/F
    //
    //  Selector: p_neg = max(motion_norm*2, occlusion_risk)
    //    Clipped to [0,1], so for zero motion and no occlusion → pure WGM.
    //
    //  Implementation: interpolate in log space (Riemannian interpolation)
    //    log F* = lerp( log F_WGM, log F_WHM, p_neg )
    //    F*     = exp( lerp( logWGM_r, logWHM_r, p_neg ) )  per channel
    //
    //  Boundedness proof:
    //    WHM ≤ GM ≤ WGM ≤ AM — all are sub-arithmetic, sub-max
    //    Log-lerp between two values both ≤ max(Fw,Ft,Fs) stays ≤ max
    //    Similarly ≥ min.  So min(Fw,Ft,Fs) ≤ F* ≤ max(Fw,Ft,Fs). QED.
    // ====================================================================

    float motion_norm = saturate(dot(finalFlow, finalFlow) / (80.0*80.0) * 2.0);
    float occ_risk    = 1.0 - c_div;
    float p_neg       = saturate(max(motion_norm, occ_risk));

    // Ensure positivity in log space (LOG_FL floor already applied above)
    float3 fw = max(Fw_l, LOG_FL);
    float3 ft = max(Ft_l, LOG_FL);
    float3 fs = max(Fs_l, LOG_FL);

    // Weighted Geometric Mean component (log-space weighted average)
    float logWGM_r = nw*log(fw.r) + nt*log(ft.r) + ns*log(fs.r);
    float logWGM_g = nw*log(fw.g) + nt*log(ft.g) + ns*log(fs.g);
    float logWGM_b = nw*log(fw.b) + nt*log(ft.b) + ns*log(fs.b);

    // Weighted Harmonic Mean component (log of 1/inv = -log(inv))
    float logWHM_r = -log(max(nw/fw.r + nt/ft.r + ns/fs.r, LOG_FL));
    float logWHM_g = -log(max(nw/fw.g + nt/ft.g + ns/fs.g, LOG_FL));
    float logWHM_b = -log(max(nw/fw.b + nt/ft.b + ns/fs.b, LOG_FL));

    // GFF: Riemannian interpolation between WGM and WHM
    float3 F_star_l;
    F_star_l.r = saturate(exp(lerp(logWGM_r, logWHM_r, p_neg)));
    F_star_l.g = saturate(exp(lerp(logWGM_g, logWHM_g, p_neg)));
    F_star_l.b = saturate(exp(lerp(logWGM_b, logWHM_b, p_neg)));

    // ---- Apply temporal consistency (G5) + confidence blend -------------
    float blend = saturate(finalConf * 2.0 * tConsist);
    float3 pred_l = lerp(c2l, F_star_l, blend);

    // ---- G6 — In-register Sharpening  (FlowFrameX, zero extra reads) ----
    // Counteract bilinear softening from TCF-rescued pixels
    float lumaBlur4  = 0.25*(my_lumaE + my_lumaW + my_lumaN + my_lumaS);
    float lumaDetail = my_lumaC - lumaBlur4;
    float rescueFrac = saturate(tileConf * (1.0 - my_w_nf) * 1.5);
    float sharpK     = rescueFrac * 0.30;
    pred_l += sharpK * lumaDetail;
    pred_l  = saturate(pred_l);

    // ---- Convert back to sRGB, write to RGBA output ---------------------
    // Input textures are BGRA (.rgb = B,G,R in HLSL).
    // After processing we have pred_l in (B,G,R) channel order.
    // Output texture is RGBA → swizzle .bgr to put (R,G,B) into (r,g,b) slots.
    float3 pred_s = sGam(pred_l);
    tOut[pos] = float4(pred_s.bgr, 1.0);
}
)HLSL";

/* ============================================================================
   Display shaders (fullscreen triangle + copy)
   ============================================================================ */
static const char* kDispVS = R"HLSL(
void main(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD0) {
    uv  = float2((id & 1) ? 2.0 : 0.0, (id & 2) ? 2.0 : 0.0);
    pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
}
)HLSL";

static const char* kDispPS = R"HLSL(
Texture2D<float4>  tSrc : register(t0);
SamplerState       gSmp : register(s0);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    return tSrc.Sample(gSmp, uv);
}
)HLSL";

/* ============================================================================
   D3D11 globals
   ============================================================================ */
static UINT g_width  = 0;
static UINT g_height = 0;

static ComPtr<ID3D11Device>           g_dev;
static ComPtr<ID3D11DeviceContext>    g_ctx;
static ComPtr<IDXGISwapChain>         g_sc;
static ComPtr<IDXGIOutputDuplication> g_dup;
static ComPtr<ID3D11RenderTargetView> g_rtv;

// Frame textures + SRVs (BGRA_UNORM — matches DXGI surface format)
static ComPtr<ID3D11Texture2D>            g_texF0, g_texF1, g_texF2;
static ComPtr<ID3D11ShaderResourceView>   g_srvF0, g_srvF1, g_srvF2;

// LK output textures (float2 flow, float quality)
static ComPtr<ID3D11Texture2D>            g_texFlow, g_texQual;
static ComPtr<ID3D11ShaderResourceView>   g_srvFlow, g_srvQual;
static ComPtr<ID3D11UnorderedAccessView>  g_uavFlow, g_uavQual;

// Prediction texture (RGBA_UNORM — guaranteed UAV support)
static ComPtr<ID3D11Texture2D>            g_texPred;
static ComPtr<ID3D11ShaderResourceView>   g_srvPred;
static ComPtr<ID3D11UnorderedAccessView>  g_uavPred;

// Shaders
static ComPtr<ID3D11ComputeShader>  g_csLK;
static ComPtr<ID3D11ComputeShader>  g_csSov;
static ComPtr<ID3D11VertexShader>   g_vsDisp;
static ComPtr<ID3D11PixelShader>    g_psDisp;

// Constant buffers
static ComPtr<ID3D11Buffer>  g_cbLK;   // {uint W,H,uint2 pad}
static ComPtr<ID3D11Buffer>  g_cbSov;  // {float captureMs,tau,W,H}

// Samplers
static ComPtr<ID3D11SamplerState>  g_samplerBilin;
static ComPtr<ID3D11SamplerState>  g_samplerPoint;

// Timing
static double g_captureMs = 33.33;
static double g_tau       = 0.5;

// Control
static HWND             g_hwnd    = nullptr;
static std::atomic<bool> g_quit  {false};
static const int HOTKEY_ID = 42;

/* ============================================================================
   Error macros
   ============================================================================ */
#define CHECK_HR(hr, msg) do { if (FAILED(hr)) { fprintf(stderr,"[KRONOS] %s  hr=%08X\n",msg,(unsigned)(hr)); ExitProcess(1); } } while(0)

/* ============================================================================
   CB update helper
   ============================================================================ */
static void UpdateCB(ID3D11Buffer* cb, const void* data, size_t sz)
{
    D3D11_MAPPED_SUBRESOURCE m;
    g_ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
    memcpy(m.pData, data, sz);
    g_ctx->Unmap(cb, 0);
}

/* ============================================================================
   Compile HLSL from string
   ============================================================================ */
static void CompileCS(const char* src, const char* entry, ComPtr<ID3D11ComputeShader>& cs)
{
    ComPtr<ID3DBlob> blob, err;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr,
                            entry, "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                            blob.GetAddressOf(), err.GetAddressOf());
    if (FAILED(hr)) {
        if (err) fprintf(stderr, "[KRONOS] CS compile error:\n%s\n", (char*)err->GetBufferPointer());
        CHECK_HR(hr, "D3DCompile CS");
    }
    CHECK_HR(g_dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, cs.GetAddressOf()),
             "CreateComputeShader");
}

static void CompileVS(const char* src, ComPtr<ID3D11VertexShader>& vs)
{
    ComPtr<ID3DBlob> blob, err;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr,
                            "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                            blob.GetAddressOf(), err.GetAddressOf());
    if (FAILED(hr)) { if (err) fprintf(stderr, "%s\n", (char*)err->GetBufferPointer()); CHECK_HR(hr,"VS"); }
    CHECK_HR(g_dev->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, vs.GetAddressOf()), "CreateVS");
}

static void CompilePS(const char* src, ComPtr<ID3D11PixelShader>& ps)
{
    ComPtr<ID3DBlob> blob, err;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr,
                            "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                            blob.GetAddressOf(), err.GetAddressOf());
    if (FAILED(hr)) { if (err) fprintf(stderr, "%s\n", (char*)err->GetBufferPointer()); CHECK_HR(hr,"PS"); }
    CHECK_HR(g_dev->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, ps.GetAddressOf()), "CreatePS");
}

/* ============================================================================
   CreateTex — helper to create texture + SRV + optional UAV
   ============================================================================ */
static void CreateTex(UINT w, UINT h, DXGI_FORMAT fmt, UINT bindFlags,
                      ID3D11Texture2D** tex,
                      ID3D11ShaderResourceView**  srv,
                      ID3D11UnorderedAccessView** uav)
{
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
    d.Format = fmt; d.SampleDesc = {1, 0};
    d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = bindFlags;
    CHECK_HR(g_dev->CreateTexture2D(&d, nullptr, tex), "CreateTexture2D");

    if (srv && (bindFlags & D3D11_BIND_SHADER_RESOURCE)) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = fmt; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        CHECK_HR(g_dev->CreateShaderResourceView(*tex, &sd, srv), "CreateSRV");
    }
    if (uav && (bindFlags & D3D11_BIND_UNORDERED_ACCESS)) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format = fmt; ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        CHECK_HR(g_dev->CreateUnorderedAccessView(*tex, &ud, uav), "CreateUAV");
    }
}

/* ============================================================================
   InitD3D — device, swap chain, hotkey window
   ============================================================================ */
static void InitD3D()
{
    // Message-only window for hotkeys
    WNDCLASSEXA wc{sizeof(wc)};
    wc.lpfnWndProc   = DefWindowProcA;
    wc.lpszClassName = "KRONOSHotkey";
    wc.hInstance     = GetModuleHandleA(nullptr);
    RegisterClassExA(&wc);
    g_hwnd = CreateWindowExA(0, "KRONOSHotkey", nullptr, 0, 0,0,0,0,
                              HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    RegisterHotKey(g_hwnd, HOTKEY_ID, MOD_CONTROL | MOD_SHIFT, 'X');

    // Enumerate DXGI adapter
    ComPtr<IDXGIFactory1> factory;
    CHECK_HR(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)factory.GetAddressOf()), "CreateDXGIFactory1");
    ComPtr<IDXGIAdapter1> adapter;
    factory->EnumAdapters1(0, adapter.GetAddressOf());

    // D3D11 device (feature level 11_0 for SM5 compute shaders)
    D3D_FEATURE_LEVEL featureLevel;
    D3D_FEATURE_LEVEL requested[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    UINT flags = 0; // set D3D11_CREATE_DEVICE_DEBUG for shader debugging
    CHECK_HR(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                               flags, requested, 2, D3D11_SDK_VERSION,
                               g_dev.GetAddressOf(), &featureLevel, g_ctx.GetAddressOf()),
             "D3D11CreateDevice");

    printf("  %s[D3D11]%s  Feature level %X   Adapter: ", C_GREEN, A_RESET, featureLevel);
    DXGI_ADAPTER_DESC1 ad{}; if (adapter) adapter->GetDesc1(&ad);
    printf("%ls\n", ad.Description); fflush(stdout);
}

/* ============================================================================
   InitDup — DXGI Desktop Duplication setup
   ============================================================================ */
static void InitDup()
{
    ComPtr<IDXGIDevice>  dxgiDev;
    ComPtr<IDXGIAdapter> dxgiAdp;
    ComPtr<IDXGIOutput>  dxgiOut;
    ComPtr<IDXGIOutput1> dxgiOut1;

    CHECK_HR(g_dev.As(&dxgiDev),                            "QueryInterface IDXGIDevice");
    CHECK_HR(dxgiDev->GetAdapter(dxgiAdp.GetAddressOf()),   "GetAdapter");
    CHECK_HR(dxgiAdp->EnumOutputs(0, dxgiOut.GetAddressOf()), "EnumOutputs");
    CHECK_HR(dxgiOut.As(&dxgiOut1),                         "QueryInterface IDXGIOutput1");
    CHECK_HR(dxgiOut1->DuplicateOutput(g_dev.Get(), g_dup.GetAddressOf()), "DuplicateOutput");

    // Detect desktop resolution
    DXGI_OUTPUT_DESC od{}; dxgiOut->GetDesc(&od);
    g_width  = od.DesktopCoordinates.right  - od.DesktopCoordinates.left;
    g_height = od.DesktopCoordinates.bottom - od.DesktopCoordinates.top;

    printf("  %s[DXGI]%s   Desktop %u x %u  (duplication active)\n",
           C_GREEN, A_RESET, g_width, g_height); fflush(stdout);
}

/* ============================================================================
   InitPipeline — textures, shaders, CBs, samplers, swap chain
   ============================================================================ */
static void InitPipeline()
{
    // --- Swap chain (RGBA, windowed overlay) ---
    ComPtr<IDXGIDevice>  dxgiDev; g_dev.As(&dxgiDev);
    ComPtr<IDXGIAdapter> dxgiAdp; dxgiDev->GetAdapter(dxgiAdp.GetAddressOf());
    ComPtr<IDXGIFactory> dxgiFactory;
    dxgiAdp->GetParent(__uuidof(IDXGIFactory), (void**)dxgiFactory.GetAddressOf());

    // Layered overlay window (WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT)
    // Allows showing prediction on top of the game
    WNDCLASSEXA owc{sizeof(owc)};
    owc.lpfnWndProc   = DefWindowProcA;
    owc.lpszClassName = "KRONOSOverlay";
    owc.hInstance     = GetModuleHandleA(nullptr);
    owc.hbrBackground = nullptr;
    owc.style         = CS_OWNDC;
    RegisterClassExA(&owc);

    HWND overlay = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_LAYERED,
        "KRONOSOverlay", "KRONOS",
        WS_POPUP | WS_VISIBLE,
        0, 0, g_width, g_height,
        nullptr, nullptr, owc.hInstance, nullptr);
    SetLayeredWindowAttributes(overlay, 0, 255, LWA_ALPHA);

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferDesc.Width  = g_width;
    scd.BufferDesc.Height = g_height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc        = {1, 0};
    scd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount       = 2;
    scd.OutputWindow      = overlay;
    scd.Windowed          = TRUE;
    scd.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;
    CHECK_HR(dxgiFactory->CreateSwapChain(g_dev.Get(), &scd, g_sc.GetAddressOf()), "CreateSwapChain");

    ComPtr<ID3D11Texture2D> backbuf;
    g_sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backbuf.GetAddressOf());
    CHECK_HR(g_dev->CreateRenderTargetView(backbuf.Get(), nullptr, g_rtv.GetAddressOf()), "CreateRTV");

    // --- Frame textures (BGRA — matches DXGI surface, SRV only) ---
    UINT bindSRV = D3D11_BIND_SHADER_RESOURCE;
    UINT bindRW  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    CreateTex(g_width, g_height, DXGI_FORMAT_B8G8R8A8_UNORM, bindSRV,
              g_texF0.GetAddressOf(), g_srvF0.GetAddressOf(), nullptr);
    CreateTex(g_width, g_height, DXGI_FORMAT_B8G8R8A8_UNORM, bindSRV,
              g_texF1.GetAddressOf(), g_srvF1.GetAddressOf(), nullptr);
    CreateTex(g_width, g_height, DXGI_FORMAT_B8G8R8A8_UNORM, bindSRV,
              g_texF2.GetAddressOf(), g_srvF2.GetAddressOf(), nullptr);

    // --- LK flow + quality textures (float, SRV+UAV) ---
    CreateTex(g_width, g_height, DXGI_FORMAT_R32G32_FLOAT, bindRW,
              g_texFlow.GetAddressOf(), g_srvFlow.GetAddressOf(), g_uavFlow.GetAddressOf());
    CreateTex(g_width, g_height, DXGI_FORMAT_R32_FLOAT, bindRW,
              g_texQual.GetAddressOf(), g_srvQual.GetAddressOf(), g_uavQual.GetAddressOf());

    // --- Prediction texture (RGBA_UNORM — guaranteed UAV support on D3D11 hardware) ---
    CreateTex(g_width, g_height, DXGI_FORMAT_R8G8B8A8_UNORM, bindRW,
              g_texPred.GetAddressOf(), g_srvPred.GetAddressOf(), g_uavPred.GetAddressOf());

    // --- Compile shaders ---
    CompileCS(kLKFlowCS,    "CSMain", g_csLK);
    CompileCS(kSovereignCS, "CSMain", g_csSov);
    CompileVS(kDispVS,  g_vsDisp);
    CompilePS(kDispPS,  g_psDisp);

    // --- Constant buffers ---
    auto MakeCB = [&](UINT sz, ComPtr<ID3D11Buffer>& cb) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth      = sz;
        bd.Usage          = D3D11_USAGE_DYNAMIC;
        bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        CHECK_HR(g_dev->CreateBuffer(&bd, nullptr, cb.GetAddressOf()), "CreateCB");
    };
    MakeCB(16, g_cbLK);   // uint W,H, uint2 pad
    MakeCB(16, g_cbSov);  // float captureMs, tau, W, H

    // --- Bilinear clamp sampler ---
    {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD         = 0;
        CHECK_HR(g_dev->CreateSamplerState(&sd, g_samplerBilin.GetAddressOf()), "CreateSampler");
    }
    // --- Point sampler (for display PS) ---
    {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter         = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD         = 0;
        CHECK_HR(g_dev->CreateSamplerState(&sd, g_samplerPoint.GetAddressOf()), "CreatePointSampler");
    }

    printf("  %s[PIPELINE]%s  Textures, shaders, CBs ready.\n", C_GREEN, A_RESET);
    fflush(stdout);
}

/* ============================================================================
   CaptureFrame  — DXGI blocking acquire (FlowFrameX zero-idle approach)
   ============================================================================ */
enum CaptureStatus { CS_NEW, CS_DUPLICATE, CS_TIMEOUT };

static CaptureStatus CaptureFrame(UINT timeoutMs)
{
    ComPtr<IDXGIResource>          res;
    DXGI_OUTDUPL_FRAME_INFO        info{};
    HRESULT hr = g_dup->AcquireNextFrame(timeoutMs, &info, res.GetAddressOf());
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return CS_TIMEOUT;
    if (FAILED(hr)) {
        // Desktop dup lost (UAC, resolution change, etc.) — reinit
        g_dup.Reset();
        ComPtr<IDXGIDevice>  dxgiDev; g_dev.As(&dxgiDev);
        ComPtr<IDXGIAdapter> dxgiAdp; dxgiDev->GetAdapter(dxgiAdp.GetAddressOf());
        ComPtr<IDXGIOutput>  dxgiOut; dxgiAdp->EnumOutputs(0, dxgiOut.GetAddressOf());
        ComPtr<IDXGIOutput1> dxgiOut1; dxgiOut.As(&dxgiOut1);
        dxgiOut1->DuplicateOutput(g_dev.Get(), g_dup.GetAddressOf());
        return CS_TIMEOUT;
    }

    bool changed = (info.LastPresentTime.QuadPart != 0);
    if (changed) {
        ComPtr<ID3D11Texture2D> surf;
        res.As(&surf);
        if (surf) g_ctx->CopyResource(g_texF2.Get(), surf.Get());
    }

    g_dup->ReleaseFrame();
    return changed ? CS_NEW : CS_DUPLICATE;
}

/* ============================================================================
   RunLKFlow  — Dispatch Pass 1: Lucas-Kanade optical flow
   ============================================================================ */
static void RunLKFlow()
{
    struct LkCB { UINT W, H; UINT pad[2]; } cb = { g_width, g_height, {0,0} };
    UpdateCB(g_cbLK.Get(), &cb, sizeof(cb));

    g_ctx->CSSetShader(g_csLK.Get(), nullptr, 0);

    ID3D11ShaderResourceView*  srvs[] = { g_srvF2.Get(), g_srvF1.Get() };
    ID3D11UnorderedAccessView* uavs[] = { g_uavFlow.Get(), g_uavQual.Get() };
    ID3D11Buffer*              cbs[]  = { g_cbLK.Get() };
    g_ctx->CSSetShaderResources(0, 2, srvs);
    g_ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
    g_ctx->CSSetConstantBuffers(0, 1, cbs);

    UINT gx = (g_width  + 15) / 16;
    UINT gy = (g_height + 15) / 16;
    g_ctx->Dispatch(gx, gy, 1);

    static ID3D11ShaderResourceView*  nullSRV[2] = {};
    static ID3D11UnorderedAccessView* nullUAV[2] = {};
    g_ctx->CSSetShaderResources(0, 2, nullSRV);
    g_ctx->CSSetUnorderedAccessViews(0, 2, nullUAV, nullptr);
    g_ctx->CSSetShader(nullptr, nullptr, 0);
}

/* ============================================================================
   RunSovereign  — Dispatch Pass 2: GFF synthesis
   ============================================================================ */
static void RunSovereign()
{
    double tau = std::max(0.25, std::min(0.75, g_tau));
    struct SovCB { float captureMs; float tau; float W; float H; } cb = {
        (float)g_captureMs, (float)tau, (float)g_width, (float)g_height
    };
    UpdateCB(g_cbSov.Get(), &cb, sizeof(cb));

    g_ctx->CSSetShader(g_csSov.Get(), nullptr, 0);

    ID3D11ShaderResourceView* srvs[] = {
        g_srvF0.Get(), g_srvF1.Get(), g_srvF2.Get(),
        g_srvFlow.Get(), g_srvQual.Get()
    };
    ID3D11UnorderedAccessView* uavs[] = { g_uavPred.Get() };
    ID3D11SamplerState*        smps[] = { g_samplerBilin.Get() };
    ID3D11Buffer*              cbs[]  = { g_cbSov.Get() };

    g_ctx->CSSetShaderResources(0, 5, srvs);
    g_ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    g_ctx->CSSetSamplers(0, 1, smps);
    g_ctx->CSSetConstantBuffers(0, 1, cbs);

    UINT gx = (g_width  + 31) / 32;
    UINT gy = (g_height +  7) / 8;
    g_ctx->Dispatch(gx, gy, 1);

    static ID3D11ShaderResourceView*  nullSRV[5] = {};
    static ID3D11UnorderedAccessView* nullUAV[1] = {};
    static ID3D11SamplerState*        nullSMP[1] = {};
    g_ctx->CSSetShaderResources(0, 5, nullSRV);
    g_ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    g_ctx->CSSetSamplers(0, 1, nullSMP);
    g_ctx->CSSetShader(nullptr, nullptr, 0);
}

/* ============================================================================
   Display  — fullscreen triangle + Present (D3D11 swap chain, zero GDI)
   ============================================================================ */
static void Display(ID3D11ShaderResourceView* srv)
{
    g_ctx->OMSetRenderTargets(1, g_rtv.GetAddressOf(), nullptr);
    D3D11_VIEWPORT vp = {0.0f, 0.0f, (float)g_width, (float)g_height, 0.0f, 1.0f};
    g_ctx->RSSetViewports(1, &vp);

    g_ctx->VSSetShader(g_vsDisp.Get(), nullptr, 0);
    g_ctx->PSSetShader(g_psDisp.Get(), nullptr, 0);
    g_ctx->PSSetShaderResources(0, 1, &srv);
    ID3D11SamplerState* smp = g_samplerPoint.Get();
    g_ctx->PSSetSamplers(0, 1, &smp);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->Draw(3, 0);

    ID3D11ShaderResourceView* n = nullptr;
    g_ctx->PSSetShaderResources(0, 1, &n);

    g_sc->Present(0, 0);   // immediate present (use Present(1,0) for vsync)
}

/* ============================================================================
   RunLoop  — main capture/generate/display loop (FlowFrameX microsecond style)
   ============================================================================ */
static void RunLoop(int targetFps)
{
    const double frameMs = 1000.0 / targetFps;
    const double halfMs  = frameMs;
    const double emaAlpha = 0.1;

    // Warmup: wait for first real frame
    while (!g_quit) {
        MSG msg; while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID) g_quit = true;
            DispatchMessageA(&msg);
        }
        if (CaptureFrame(100) == CS_NEW) break;
    }
    if (g_quit) return;

    // Bootstrap frame history
    g_ctx->CopyResource(g_texF1.Get(), g_texF2.Get());
    g_ctx->CopyResource(g_texF0.Get(), g_texF2.Get());

    g_captureMs = 1000.0 / (targetFps / 2.0);
    g_tau       = 0.5;

    // Initial generation (avoids first-frame display glitch)
    RunLKFlow();
    RunSovereign();

    int    outCount = 0, capCount = 0, idleCount = 0;
    auto   statTick    = Clock::now();
    auto   lastCapTime = Clock::now();
    auto   nextSlot    = Clock::now();
    bool   showReal    = true;
    CaptureStatus lastStatus = CS_NEW;

    printf("\n  %s->%s  Running! Press Ctrl+Shift+X to stop.\n\n\n\n", C_GOLD2, A_RESET);
    fflush(stdout);

    while (!g_quit)
    {
        MSG msg; while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID) g_quit = true;
            DispatchMessageA(&msg);
        }
        if (g_quit) break;

        if (showReal)
        {
            // --- Show real frame, generate prediction ---
            if (lastStatus == CS_NEW) {
                g_tau = std::max(0.25, std::min(0.75, halfMs / g_captureMs));
                RunLKFlow();
                RunSovereign();
            } else {
                idleCount++;
            }
            Display(g_srvF2.Get());   // real frame
            outCount++;
            showReal = false;
        }
        else
        {
            // --- Show predicted frame ---
            if (lastStatus == CS_NEW)
                Display(g_srvPred.Get());
            else
                Display(g_srvF2.Get());
            outCount++;

            // Rotate frame history AFTER showing predicted frame
            if (lastStatus == CS_NEW) {
                g_ctx->CopyResource(g_texF0.Get(), g_texF1.Get());  // F0 ← F1
                g_ctx->CopyResource(g_texF1.Get(), g_texF2.Get());  // F1 ← F2
            }

            // Capture next frame while in timing slot
            auto now0     = Clock::now();
            auto slotEnd  = nextSlot + std::chrono::microseconds((long long)(halfMs * 1000.0));
            auto remainUs = std::chrono::duration_cast<std::chrono::microseconds>(slotEnd - now0).count();
            int  capTimeout = (remainUs > 2000) ? (int)(remainUs / 1000) : 2;

            lastStatus = CaptureFrame((UINT)capTimeout);

            if (lastStatus == CS_NEW) {
                capCount++;
                auto now1 = Clock::now();
                double measured = Dur(now1 - lastCapTime).count();
                lastCapTime = now1;
                if (measured > 1.0 && measured < 500.0)
                    g_captureMs = emaAlpha * measured + (1.0 - emaAlpha) * g_captureMs;
            }

            showReal = true;
        }

        // --- Microsecond-precise frame pacing (FlowFrameX technique) ---
        nextSlot += std::chrono::microseconds((long long)(halfMs * 1000.0));
        {
            auto sleepUntil = nextSlot - std::chrono::microseconds(300);
            while (!g_quit && Clock::now() < sleepUntil)
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            while (!g_quit && Clock::now() < nextSlot)
                _mm_pause();
        }

        // --- Stats update every second ---
        double elapsed = Dur(Clock::now() - statTick).count();
        if (elapsed >= 1000.0) {
            g_fpsOut = outCount  / (elapsed / 1000.0);
            g_fpsCap = capCount  / (elapsed / 1000.0);
            bool idle = (idleCount > capCount);
            outCount = capCount = idleCount = 0;
            statTick = Clock::now();
            PrintStats(g_fpsCap, g_fpsOut, g_captureMs, idle);
        }
    }

    printf("\n\n  %s[STOPPED]%s  KRONOS engine halted.%s\n\n", C_RED, C_LGREY, A_RESET);
    printf(A_SHOW_CURSOR); fflush(stdout);
}

/* ============================================================================
   main
   ============================================================================ */
int main()
{
    InitConsole();
    ShowSplash();

    EngineConfig cfg = ShowMenu();
    ShowRunning(cfg.fps);

    // Elevate scheduling priority
    timeBeginPeriod(1);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    // Init pipeline
    InitD3D();
    InitDup();
    InitPipeline();

    RunLoop(cfg.fps);

    UnregisterHotKey(g_hwnd, HOTKEY_ID);
    timeEndPeriod(1);
    return 0;
}

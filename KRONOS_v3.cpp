/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║            K R O N O S   v 3 . 0  —  "The Undefeatable Frame"          ║
 * ║         Generalized Fréchet Frame Synthesis Engine                      ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  Lineage:                                                                ║
 * ║    KRONOS v2.0  — 12 bugs fixed                                         ║
 * ║    NEXUS v1.1   — "SOVEREIGN" (CUDA → D3D11, LK Flow, WHM, FLGE, FASW) ║
 * ║    FlowFrameX v8.0 — "APEX"  (D3D11, DXGI, TCF, Gate5/6)              ║
 * ║                                                                          ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║  v3.0 NEW BUG-FIX LOG  (all v2.0 fixes carried forward)                ║
 * ║  ─────────────────────────────────────────────────────                  ║
 * ║  [BUG-13][CRITICAL]   tau formula gave 0.25 at steady state instead     ║
 * ║    of the intended 0.50.                                                 ║
 * ║    halfMs = frameMs/2 = captureMs/4.                                    ║
 * ║    v2: tau = halfMs / captureMs = captureMs/4 / captureMs = 0.25.      ║
 * ║    The FLGE/FASW/warp extrapolated only ¼ of the way to the next        ║
 * ║    frame.  Synthesised frames appeared glued to the real frame,         ║
 * ║    making motion look 25% as smooth as intended.                        ║
 * ║    FIXED: tau = 2.0 * halfMs / captureMs                               ║
 * ║           = captureMs/4 * 2 / captureMs = 0.50. ✓                     ║
 * ║                                                                          ║
 * ║  [BUG-14][SIGNIFICANT] FASW PDE CFL numerical instability.              ║
 * ║    cSq = 4 / (1+5000·G) → 4 for flat regions (G→0).                   ║
 * ║    coeff = cSq · τ²/2.  At τ=0.5: coeff = 0.5.                        ║
 * ║    2-D Laplacian CFL stability requires coeff · (Dxx+Dyy) ≤ 0.5.      ║
 * ║    With Dxx+Dyy ≈ 1.15 (worst case): 0.5·1.15 = 0.575 > 0.5 →        ║
 * ║    marginal instability; saturate() hard-clips ringing, creating        ║
 * ║    block artefacts in flat regions (sky, walls, HUD backdrops).        ║
 * ║    FIXED: coeff = min(cSq · τ²/2, 0.24) → stable for all τ, G.       ║
 * ║                                                                          ║
 * ║  [BUG-15][SIGNIFICANT] Overlay → Capture feedback loop.                 ║
 * ║    KRONOS rendered its synthesised frames onto a topmost WS_EX_TOPMOST  ║
 * ║    overlay.  DXGI Desktop Duplication captures the COMPOSITED desktop,  ║
 * ║    which includes the overlay.  On the next CaptureFrame() call, F2     ║
 * ║    contained KRONOS's OWN previous output.  The LK flow then detected   ║
 * ║    KRONOS-introduced pixel differences as scene motion, diverging        ║
 * ║    progressively over time → ghosting, smearing, runaway flow.         ║
 * ║    FIXED: SetWindowDisplayAffinity(overlay, WDA_EXCLUDEFROMCAPTURE).   ║
 * ║    (Win 10 2004+ / build 19041+; silently skipped on older systems.)    ║
 * ║                                                                          ║
 * ║  [BUG-16][MODERATE]   No DPI awareness declaration.                     ║
 * ║    On a 125 % / 150 % scaled monitor the overlay HWND was created with  ║
 * ║    virtualised (scaled-down) coordinates.  g_width/g_height from DXGI   ║
 * ║    were in physical pixels; the window was in logical pixels →          ║
 * ║    overlay covered only ~44 % of the screen area at 150 % DPI.         ║
 * ║    FIXED: SetProcessDpiAwarenessContext(                                ║
 * ║             DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)                ║
 * ║           called before any HWND creation.                              ║
 * ║                                                                          ║
 * ║  [BUG-17][MODERATE]   CaptureFrame() CopyResource silent mismatch.     ║
 * ║    CopyResource requires identical Width × Height × Format × Mips.     ║
 * ║    On HDR (DXGI_FORMAT_R16G16B16A16_FLOAT) or                          ║
 * ║    DXGI_FORMAT_B8G8R8X8_UNORM outputs the copy silently fails,         ║
 * ║    leaving F2 black.  Engine then computes flow on black→black →        ║
 * ║    zero optical flow everywhere, synthesis outputs F2 (blank).         ║
 * ║    FIXED: QueryInterface the acquired surface's DESC; verify format     ║
 * ║    and dimensions; log mismatch to stderr and return CS_TIMEOUT.       ║
 * ║                                                                          ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║  v3.0 ENHANCEMENTS                                                      ║
 * ║  ─────────────────                                                      ║
 * ║  + 5-line live stats dashboard:                                          ║
 * ║      capture fps / output fps  ·  multiplier                            ║
 * ║      tau  ·  synthesis accuracy %  ·  capture→display latency (µs)     ║
 * ║      VRAM used / budget (MB)  [via IDXGIAdapter3 — Win 10 only]        ║
 * ║  + IDXGIAdapter3 global for VRAM telemetry (null-safe fallback)         ║
 * ║  + Overlay HWND stored globally; proper WM lifecycle                    ║
 * ║  + SetPriorityClass return value checked; stderr warning on failure      ║
 * ║  + timeBeginPeriod(1) return checked                                    ║
 * ║                                                                          ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║  Architecture — Three Layers, One Engine                                ║
 * ║  ─────────────────────────────────────────                              ║
 * ║  01  Capture Layer   Half-rate DXGI Desktop Dup.  If target=60 fps,    ║
 * ║                      engine captures ~30 fps, halving CPU overhead.     ║
 * ║  02  Input Pass-Thru WS_EX_TRANSPARENT + WDA_EXCLUDEFROMCAPTURE.       ║
 * ║                      Every keystroke/mousemove goes to the game.        ║
 * ║  03  Live CLI Stats  5-line dashboard: fps, VRAM, latency, accuracy.   ║
 * ║                                                                          ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║  MATHEMATICS (unchanged from v2 — GFF, Three Pillars, Six Gates)        ║
 * ║  M_p(Fw,Ft,Fs) = exp(lerp(logF_WGM, logF_WHM, p_neg))                  ║
 * ║  THREE PILLARS: A AnisoBilinear Warp · B FLGE · C FASW                  ║
 * ║  SIX GATES:    G1 JOD · G2 OFC · G3 DIV · G4 TCF · G5 TC · G6 SHARP  ║
 * ║                                                                          ║
 * ║  Target: GT 730 GF108 Fermi (DDR3 64-bit)  i3-2120  6 GB DDR3          ║
 * ║  API:    D3D11 CS 5.0  DXGI 1.2  Windows 8+                            ║
 * ║  Build:  cl /O2 /EHsc /MT /arch:SSE2 KRONOS_v3.cpp                     ║
 * ║          (link: d3d11 dxgi d3dcompiler winmm)                           ║
 * ║                                                                          ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shellscalingapi.h>   // SetProcessDpiAwarenessContext (Win 8.1+)
#include <mmsystem.h>
#include <d3d11.h>
#include <dxgi1_4.h>           // IDXGIAdapter3 for VRAM query (DXGI 1.4 / Win 10)
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
#pragma comment(lib, "shcore.lib")   // for SetProcessDpiAwareness fallback

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
#define C_ORANGE "\033[38;2;255;160;50m"
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

/* ============================================================================
   FIX [BUG-16]: DPI awareness — must be called before any HWND creation.
   SetProcessDpiAwarenessContext is Win 10 1607+ (build 14393).
   Fall back to SetProcessDpiAwareness (Win 8.1+) on older systems.
   Without this, on a 150% DPI monitor the overlay covered ~44% of screen.
   ============================================================================ */
static void InitDpiAwareness()
{
    // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = -4 (defined in Win 10 SDK)
    typedef BOOL (WINAPI *PFNSETPROCESSDPIAWARENESSCONTEXT)(HANDLE);
    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (user32) {
        auto fn = (PFNSETPROCESSDPIAWARENESSCONTEXT)
                   GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (fn) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = (HANDLE)-4
            if (fn((HANDLE)(LONG_PTR)-4)) return;
        }
    }
    // Fallback: SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE = 2)
    typedef HRESULT (WINAPI *PFNSETPROCESSDPIAWARENESS)(int);
    HMODULE shcore = LoadLibraryA("shcore.dll");
    if (shcore) {
        auto fn2 = (PFNSETPROCESSDPIAWARENESS)
                    GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (fn2) fn2(2);
        FreeLibrary(shcore);
    }
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
        SMALL_RECT r = {0, 0, 94, 40}; SetConsoleWindowInfo(hOut, TRUE, &r);
    }
    SetConsoleTitleA("KRONOS v3.0  |  The Undefeatable Frame  [GFF + LK-SOVEREIGN + APEX]");
}

static void ShowSplash()
{
    printf(A_CLEAR A_HIDE_CURSOR "\n\n");

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
    printf("  %s%s  v 3 . 0   —   T h e   U n d e f e a t a b l e   F r a m e%s\n\n",
           C_VIOLET, A_DIM, A_RESET);
    SleepMs(80);

    printf("  %s%sGeneralized Fréchet Synthesis  ·  LK Flow  ·  APEX TCF  ·  GFF Fusion%s\n",
           C_GREY, A_DIM, A_RESET);
    printf("  %s%s5 New Bug Fixes  ·  Enhanced Stats  ·  GT 730 / i3-2120 Target%s\n\n",
           C_GREY, A_DIM, A_RESET);
    SleepMs(80);
    HRule(72, C_GOLD3);
    SleepMs(60);

    struct { const char* step; const char* detail; } steps[] = {
        {"D3D11 device + DXGI Desktop Dup      ", "GPU-native capture, zero PCIe round-trip"          },
        {"LK Flow CS  (5×5 shm-tile, HLSL)     ", "BGRA-correct luma, Harris quality, c_div"          },
        {"SOVEREIGN CS  (FLGE+FASW+AnisoWarp)  ", "τ=0.50 corrected, FASW CFL-stable, BT.709 luma"    },
        {"APEX TCF  (3 KB groupshared, 8 bars) ", "Tile-Consensus Flow, flat/pan region fix"           },
        {"GFF Fusion  (adaptive p∈[−1,0])      ", "WGM↔WHM: colour-safe ↔ ghost-proof"               },
        {"Gates 1-6  (JOD·OFC·DIV·TCF·TC·SHP) ", "OFC: brightness-constancy residual"                },
        {"Overlay  (WDA_EXCLUDEFROMCAPTURE)     ", "Feedback loop closed — no self-capture [BUG-15]"   },
        {"DPI Awareness  (per-monitor v2)       ", "Physical pixel coords — full coverage [BUG-16]"    },
        {"Swap-chain Present + µs spin-wait     ", "Microsecond frame pacing, EMA timing"              },
        {"Stats: VRAM · Latency · Accuracy      ", "5-line live dashboard — IDXGIAdapter3"             },
    };
    SleepMs(40);
    for (auto& s : steps) {
        TypeWrite("  > ", 5000, C_LGREY);
        TypeWrite(s.step, 4000, C_WHITE);
        printf("%s  %s%s%s\n", C_GREEN, A_DIM, s.detail, A_RESET);
        fflush(stdout); SleepMs(28);
    }

    AnimBar(42, 700, "KRONOS v3 ENGINE READY");
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
    printf("  %s%s  KRONOS v3.0  —  RUNNING%s\n\n", C_GOLD1, A_BOLD, A_RESET);
    HRule(68, C_GOLD3);
    auto Row = [](const char* icon, const char* lbl, const char* val, const char* vc = C_GREEN) {
        printf("  %s%s%s  %s%-26s%s  %s%s%s%s\n",
               C_TEAL, icon, A_RESET, C_LGREY, lbl, A_RESET, vc, A_BOLD, val, A_RESET);
        fflush(stdout);
    };
    char b[64];
    snprintf(b, sizeof(b), "%d fps out  (capture ~%d fps)", fps, fps/2);
    Row("\xe2\x9a\xa1", "Target output",       b,                                              C_GOLD2);
    Row("\xf0\x9f\x94\xae", "Algorithm",       "KRONOS GFF — WGM\xe2\x86\x94WHM adaptive",    C_VIOLET);
    Row("\xf0\x9f\x8c\x8a", "Optical flow",    "LK 5\xc3\x97" "5 BGRA-correct + APEX TCF",      C_CYAN);
    Row("\xe2\x9c\xa8", "Pillars",              "AnisoWarp + FLGE(\xcf\x84=0.50) + FASW(CFL)", C_CYAN);
    Row("\xf0\x9f\x94\x92", "Capture",         "DXGI Dup  \xe2\x86\x92  WDA_EXCLUDEFROMCAPTURE", C_GREEN);
    Row("\xe2\x8f\xb1", "Timing",              "\xc2\xb5s spin-wait + EMA + timeBeginPeriod(1)", C_GREEN);
    Row("\xe2\x8c\xa8", "Stop hotkey",         "Ctrl+Shift+X",                                C_RED);
    printf("\n"); HRule(68, C_GOLD3); printf("\n");
    printf("  %s%s  Waiting for first frame...%s\n", C_GREY, A_DIM, A_RESET);
    /* Reserve exactly 5 lines for the stats dashboard (+1 blank below header) */
    printf("\n\n\n\n\n\n");
    fflush(stdout);
}

/* ============================================================================
   Live stats  — 5-line dashboard
   Lines 1-5 are reserved below the "Waiting..." line.
   \033[5A goes back up 5 lines to overwrite them every second.
   ============================================================================ */
static double g_fpsOut    = 0.0;
static double g_fpsCap    = 0.0;
static double g_tau_cur   = 0.5;
static double g_latencyUs = 0.0;   // capture → first display, µs
static double g_accuracy  = 0.0;   // % synth slots that used pred (not fallback F2)

static void PrintStats(double capFps, double outFps, double captureMs,
                       bool idle, double tau, double latUs, double acc,
                       UINT64 vramUsed, UINT64 vramBudget)
{
    // Move cursor up 5 lines to overwrite the reserved stats area.
    printf("\033[5A");

    // Line 1 — fps
    printf("  %s%-24s%s  %s%.1f%s/%s%.1f%s fps  cap %.1fms  %s%s%s\n",
           C_TEAL, "capture / output fps", A_RESET,
           C_GOLD1 A_BOLD, capFps, A_RESET, C_GOLD2, outFps, A_RESET,
           captureMs,
           idle ? C_GREY : C_GREEN, idle ? "IDLE" : "ACTIVE", A_RESET);

    // Line 2 — multiplier + tau
    double mult = outFps / std::max(capFps, 0.1);
    printf("  %sMultiplier%s %s%.2fx%s   %s\xcf\x84%s %s%.3f%s  %s(Ctrl+Shift+X to stop)%s\n",
           C_LGREY, A_RESET, C_VIOLET A_BOLD, mult, A_RESET,
           C_LGREY, A_RESET, C_ORANGE A_BOLD, tau, A_RESET,
           C_GREY, A_RESET);

    // Line 3 — accuracy + latency
    printf("  %sSynth accuracy%s %s%.1f%%%s   %sCap\xe2\x86\x92" "Display%s %s%.0f\xc2\xb5s%s\n",
           C_LGREY, A_RESET, C_GREEN A_BOLD, acc, A_RESET,
           C_LGREY, A_RESET, C_CYAN A_BOLD, latUs, A_RESET);

    // Line 4 — VRAM
    if (vramBudget > 0) {
        double usedMB   = vramUsed   / (1024.0 * 1024.0);
        double budgetMB = vramBudget / (1024.0 * 1024.0);
        double pct      = 100.0 * usedMB / std::max(budgetMB, 1.0);
        const char* vc  = (pct > 85.0) ? C_RED : (pct > 65.0) ? C_ORANGE : C_GREEN;
        printf("  %sVRAM%s  %s%s%.0f%s/%s%.0f MB%s  (%.0f%%)\n",
               C_LGREY, A_RESET, vc, A_BOLD, usedMB, A_RESET,
               C_LGREY, budgetMB, A_RESET, pct);
    } else {
        printf("  %sVRAM%s  %s(IDXGIAdapter3 not available on this OS)%s\n",
               C_LGREY, A_RESET, C_GREY, A_RESET);
    }

    // Line 5 — engine tag
    printf("  %s[KRONOS v3] LK+GFF+APEX  \xcf\x84-fix\xc2\xb7" "FASW-CFL\xc2\xb7NoFeedback\xc2\xb7" "DPI-aware%s\n",
           C_CYAN A_DIM, A_RESET);

    fflush(stdout);
}

/* ============================================================================
   HLSL PASS 1  —  Lucas-Kanade Optical Flow
   ============================================================================
   True 2-D LK per pixel from 5×5 structure tensor.
   Closed-form 2×2 solve.  Harris corner quality.  Shared-mem tile loading.

   Tile dims: LK_TW=22, LK_TH=22  (block 16×16, PAD=3)
   Shared memory: 2 tiles × 484 floats × 4 B = 3 872 B  (Fermi: 48 KB/SM)

   Bugs fixed from v2 (all carried forward, no new LK changes in v3):
     [BUG-02] luma() BGRA-correct: dot(c, float3(0.114, 0.587, 0.299))
   ============================================================================ */
static const char* kLKFlowCS = R"HLSL(
// Lucas-Kanade 5x5 Optical Flow — HLSL Compute Shader
// KRONOS v3  Pass 1: BGRA-correct luma, shared-memory tile loading

#define LK_R      2
#define LK_PAD    3
#define BLOCK_W   16
#define BLOCK_H   16
#define LK_TW     22
#define LK_TH     22
#define MAX_FLOW  80.0
#define LK_HSCALE 8000.0

Texture2D<float4>    tF2    : register(t0);
Texture2D<float4>    tF1    : register(t1);
RWTexture2D<float2>  tFlow  : register(u0);
RWTexture2D<float>   tQual  : register(u1);

cbuffer LKCB : register(b0) { uint lkW; uint lkH; uint2 lkPad; }

groupshared float sL2[LK_TH][LK_TW];
groupshared float sL1[LK_TH][LK_TW];

// [BUG-02] DXGI BGRA -> HLSL .r=B, .g=G, .b=R
// BT.601 gamma-domain luma: Y = 0.114B + 0.587G + 0.299R
float luma(float3 c) { return dot(c, float3(0.114, 0.587, 0.299)); }

[numthreads(BLOCK_W, BLOCK_H, 1)]
void CSMain(uint3 gid : SV_GroupID,
            uint3 tid : SV_GroupThreadID,
            uint3 did : SV_DispatchThreadID)
{
    const int bx = (int)(gid.x * BLOCK_W);
    const int by = (int)(gid.y * BLOCK_H);
    const int threadLinear = tid.y * BLOCK_W + tid.x;
    const int tileTotal    = LK_TW * LK_TH;

    // Cooperative shared-memory tile load (all 256 threads)
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

    float sxx = 0, syy = 0, sxy = 0, sxt = 0, syt = 0;

    [unroll]
    for (int dy = -LK_R; dy <= LK_R; dy++) {
        [unroll]
        for (int dx = -LK_R; dx <= LK_R; dx++) {
            int lx = tid.x + LK_PAD + dx;
            int ly = tid.y + LK_PAD + dy;
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

    float det = sxx * syy - sxy * sxy;
    float tr  = sxx + syy + 1e-7;
    float harris = saturate((det / (tr * tr)) * LK_HSCALE);

    float2 flow = float2(0, 0);
    if (abs(det) > 1e-8) {
        flow.x = -(syy * sxt - sxy * syt) / det;
        flow.y = -(sxx * syt - sxy * sxt) / det;
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
   All v2.0 fixes carried forward.
   v3.0 NEW fix:
     [BUG-14] FASW CFL stability: coeff = min(cSq * τ²/2, 0.24)
              Previously coeff could reach 0.5 for flat regions (G→0, τ=0.5),
              exceeding the 2-D CFL stability threshold of ~0.43, causing
              saturate()-clipped ringing visible as blocky artefacts in
              large flat areas (sky, UI backgrounds, clean walls).
              Clamping to 0.24 ensures numerical stability for all τ ∈ [0,1]
              and all gradient magnitudes.
   ============================================================================ */
static const char* kSovereignCS = R"HLSL(
// KRONOS v3 SOVEREIGN Synthesis Shader

Texture2D<float4>    tF0    : register(t0);
Texture2D<float4>    tF1    : register(t1);
Texture2D<float4>    tF2    : register(t2);
Texture2D<float2>    tFlowT : register(t3);
Texture2D<float>     tQualT : register(t4);
RWTexture2D<float4>  tOut   : register(u0);
SamplerState         gBilin : register(s0);

cbuffer SovCB : register(b0)
{
    float captureMs;
    float tau;
    float sovW;
    float sovH;
}

// APEX TCF groupshared — 3×256×4 = 3 072 bytes (Fermi: 48 KB/SM)
groupshared float gs_fx[256];
groupshared float gs_fy[256];
groupshared float gs_fw[256];

// sRGB (IEC 61966-2-1, pow 2.4)
float lin1(float c) { return (c <= 0.04045) ? c / 12.92 : pow(max((c + 0.055) / 1.055, 0.0), 2.4); }
float gam1(float c) { c = max(c, 0.0); return (c <= 0.0031308) ? c * 12.92 : 1.055 * pow(c, 1.0/2.4) - 0.055; }
float3 sLin(float3 c) { return float3(lin1(c.r), lin1(c.g), lin1(c.b)); }
float3 sGam(float3 c) { return float3(gam1(c.r), gam1(c.g), gam1(c.b)); }

// [BUG-02] DXGI BGRA -> .r=B,.g=G,.b=R  — BT.601 gamma luma
float luma(float3 c) { return dot(c, float3(0.114, 0.587, 0.299)); }
// [BUG-03] BT.709 linear luma for BGRA channels (0.0722,0.7152,0.2126)
float lumaLin(float3 c) { return dot(c, float3(0.0722, 0.7152, 0.2126)); }

// [BUG-04] Anisotropic backward warp — corrected eps_along, sigma_perp
float3 AnisoBilinear(float2 uv_center, float2 motion)
{
    float2 invD = 1.0 / float2(sovW, sovH);
    float vlen = length(motion);
    if (vlen < 0.3)
        return tF2.SampleLevel(gBilin, uv_center, 0).rgb;

    float2 mh = motion / vlen;
    float2 mp = float2(-mh.y, mh.x);

    const float inv_ee = 1.0 / (0.70 * 0.70);
    const float inv_ss = 1.0 / (0.40 * 0.40);

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

    float3 c2s = tF2.Load(int3(pos, 0)).rgb;
    float3 c1s = tF1.Load(int3(pos, 0)).rgb;
    float3 c0s = tF0.Load(int3(pos, 0)).rgb;

    // [BUG-02] Static pixel detection with BGRA-correct luma
    float earlyIt  = abs(luma(c2s) - luma(c1s));
    float earlyVel = luma(abs(c2s - c1s));
    bool  isStatic = oob || (earlyIt < 0.004 && earlyVel < 0.004);

    float2 localFlow = tFlowT.Load(int3(pos, 0));
    float  lk_q      = tQualT.Load(int3(pos, 0));

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
        my_w_nf = w_flat * w_outl * lk_q;
    }

    // APEX TCF: write to groupshared (all threads, incl. static/OOB)
    gs_fx[groupIdx] = isStatic ? 0.0 : localFlow.x * my_w_nf;
    gs_fy[groupIdx] = isStatic ? 0.0 : localFlow.y * my_w_nf;
    gs_fw[groupIdx] = isStatic ? 0.0 : my_w_nf;

    // 8-stage binary tree reduction (ALL 256 threads participate)
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
    float  tileConf = saturate(tileW / 12.0);

    // Early exit after all barriers
    if (oob) return;
    if (isStatic) {
        tOut[pos] = float4(c2s.bgr, 1.0);
        return;
    }

    // APEX TCF: blend local flow with tile consensus
    float  tileBlend = saturate(tileConf * (1.0 - my_w_nf));
    float2 finalFlow = lerp(localFlow, tileFlow, tileBlend);
    float  finalConf = saturate(my_w_nf + tileConf*(1.0-my_w_nf)*0.6);

    // G3 Divergence confidence
    int2 xl = int2(max(pos.x-1, 0), pos.y),        xr = int2(min(pos.x+1,(int)W-1), pos.y);
    int2 yu = int2(pos.x, max(pos.y-1, 0)),         yd = int2(pos.x, min(pos.y+1,(int)H-1));
    float dvx = (tFlowT.Load(int3(xr,0)).x - tFlowT.Load(int3(xl,0)).x) * 0.5;
    float dvy = (tFlowT.Load(int3(yd,0)).y - tFlowT.Load(int3(yu,0)).y) * 0.5;
    float divV  = dvx + dvy;
    float c_div = exp(-divV*divV * (1.0/(0.40*0.40)));
    float w_w   = saturate(c_div * lk_q * (1.0 + tileConf*(1.0 - c_div*lk_q)));

    // Load 4-neighbours for F2
    float3 c2E = tF2.Load(int3(nb_e,0)).rgb;
    float3 c2W = tF2.Load(int3(nb_w,0)).rgb;
    float3 c2N = tF2.Load(int3(nb_n,0)).rgb;
    float3 c2S = tF2.Load(int3(nb_s,0)).rgb;

    // Convert to LINEAR LIGHT (IEC 61966-2-1)
    float3 c2l  = sLin(c2s);
    float3 c1l  = sLin(c1s);
    float3 c0l  = sLin(c0s);
    float3 c2El = sLin(c2E);
    float3 c2Wl = sLin(c2W);
    float3 c2Nl = sLin(c2N);
    float3 c2Sl = sLin(c2S);

    float3 vel = c2l - c1l;
    float3 acc = c2l - 2.0*c1l + c0l;
    float  VV  = dot(vel, vel);
    float  AA  = dot(acc, acc);

    // G1 JOD [BUG-08]: relJ = AA/(VV+AA+eps) — stable in noise
    float relJ   = AA / (VV + AA + 1e-5);
    float w_jerk = saturate(relJ * 2.0);

    // Linear luminance gradient [BUG-03] lumaLin on linear values
    float lumaC_l = lumaLin(c2l);
    float Lx      = (lumaLin(c2El) - lumaLin(c2Wl)) * 0.5;
    float Ly      = (lumaLin(c2Sl) - lumaLin(c2Nl)) * 0.5;
    float G       = Lx*Lx + Ly*Ly + 1e-6;
    float h       = saturate(G * 0.0008);

    // G2 OFC [BUG-05]: brightness-constancy residual
    float It_lin  = lumaLin(c2l) - lumaLin(c1l);
    float bc_res  = It_lin + Lx * finalFlow.x + Ly * finalFlow.y;
    float ofc     = saturate(bc_res * bc_res / (G + It_lin*It_lin + 1e-6));

    float w_t     = (1.0 - w_jerk) * h;
    float w_s     = saturate(w_jerk + 0.30*(1.0 - h) + ofc * 0.5);
    float W_total = w_w + w_t + w_s + 1e-7;
    float nw = w_w / W_total, nt = w_t / W_total, ns = w_s / W_total;

    // G5 Temporal Consistency
    float It      = luma(c2s) - luma(c1s);
    float It_prev = luma(c1s) - luma(c0s);
    float tConsist = (It * It_prev >= 0.0) ? 1.0 : 0.3;

    // ====================================================================
    // PILLAR A — Anisotropic Backward Warp [BUG-04 fixed eps/sigma]
    // ====================================================================
    float2 warpUV = (float2(pos) - finalFlow * tau + 0.5) / float2(sovW, sovH);
    float3 Fw_s   = AnisoBilinear(warpUV, finalFlow);
    float3 Fw_l   = sLin(Fw_s);

    // ====================================================================
    // PILLAR B — FLGE Log-Geodesic Temporal [BUG-06 dynamic coefficients]
    // a = 1+τ+τ²/2, b = -(τ+τ²), c = τ²/2  — partition: a+b+c = 1 ✓
    // ====================================================================
    const float LOG_FL    = 1e-4;
    const float RATIO_CAP = 3.0;
    float flge_a = 1.0 + tau + 0.5*tau*tau;
    float flge_b = -(tau + tau*tau);
    float flge_c = 0.5 * tau * tau;
    float3 Ft_l;
    Ft_l.r = exp(flge_a*log(max(c2l.r,LOG_FL)) + flge_b*log(max(c1l.r,LOG_FL)) + flge_c*log(max(c0l.r,LOG_FL)));
    Ft_l.g = exp(flge_a*log(max(c2l.g,LOG_FL)) + flge_b*log(max(c1l.g,LOG_FL)) + flge_c*log(max(c0l.g,LOG_FL)));
    Ft_l.b = exp(flge_a*log(max(c2l.b,LOG_FL)) + flge_b*log(max(c1l.b,LOG_FL)) + flge_c*log(max(c0l.b,LOG_FL)));
    Ft_l = min(Ft_l, c2l * RATIO_CAP + LOG_FL);
    Ft_l = saturate(Ft_l);

    // ====================================================================
    // PILLAR C — FASW Anisotropic Spatial Wave PDE
    // [BUG-07] dynamic coeff from live tau  [v2]
    // [BUG-14] CFL stability clamp — NEW in v3
    //
    // 2-D CFL for explicit Laplacian step requires coeff*(Dxx+Dyy) ≤ 0.5.
    // Worst case (Dxx+Dyy ≈ 1.15, flat region) needs coeff ≤ 0.43.
    // Clamping to 0.24 gives a comfortable stability margin for all τ, G.
    // Without the clamp: coeff = cSq*τ²/2 up to 4*0.125 = 0.5 at τ=0.5
    // in flat regions, causing saturate()-clipped ringing (block artefacts).
    // ====================================================================
    float Dxx  = 1.0 - 0.85*(Lx*Lx)/G;
    float Dyy  = 1.0 - 0.85*(Ly*Ly)/G;
    float cSq  = 4.0 / (1.0 + 5000.0*G);
    float coeff = min(cSq * (0.5 * tau * tau), 0.24);  // FIX [BUG-14]
    float3 aLap = (c2El + c2Wl - 2.0*c2l)*Dxx + (c2Nl + c2Sl - 2.0*c2l)*Dyy;
    float3 Fs_l = saturate(c2l + aLap * coeff);

    // ====================================================================
    // GFF — GENERALIZED FRÉCHET FUSION (unchanged mathematics)
    // M_p = exp(lerp(logWGM, logWHM, p_neg))
    // p_neg driven by motion intensity + occlusion risk
    // ====================================================================
    float motion_norm = saturate(dot(finalFlow, finalFlow) / (80.0*80.0) * 2.0);
    float occ_risk    = 1.0 - c_div;
    float p_neg       = saturate(max(motion_norm, occ_risk));

    float3 fw = max(Fw_l, LOG_FL);
    float3 ft = max(Ft_l, LOG_FL);
    float3 fs = max(Fs_l, LOG_FL);

    float logWGM_r = nw*log(fw.r) + nt*log(ft.r) + ns*log(fs.r);
    float logWGM_g = nw*log(fw.g) + nt*log(ft.g) + ns*log(fs.g);
    float logWGM_b = nw*log(fw.b) + nt*log(ft.b) + ns*log(fs.b);

    float logWHM_r = -log(max(nw/fw.r + nt/ft.r + ns/fs.r, LOG_FL));
    float logWHM_g = -log(max(nw/fw.g + nt/ft.g + ns/fs.g, LOG_FL));
    float logWHM_b = -log(max(nw/fw.b + nt/ft.b + ns/fs.b, LOG_FL));

    float3 F_star_l;
    F_star_l.r = saturate(exp(lerp(logWGM_r, logWHM_r, p_neg)));
    F_star_l.g = saturate(exp(lerp(logWGM_g, logWHM_g, p_neg)));
    F_star_l.b = saturate(exp(lerp(logWGM_b, logWHM_b, p_neg)));

    float blend = saturate(finalConf * 2.0 * tConsist);
    float3 pred_l = lerp(c2l, F_star_l, blend);

    // G6 Sharpening [BUG-09]: lumaLin on linear neighbours
    float lumaBlur4_l = 0.25*(lumaLin(c2El) + lumaLin(c2Wl) +
                               lumaLin(c2Nl) + lumaLin(c2Sl));
    float lumaDetail_l = lumaC_l - lumaBlur4_l;
    float rescueFrac   = saturate(tileConf * (1.0 - my_w_nf) * 1.5);
    float sharpK       = rescueFrac * 0.30;
    pred_l += sharpK * lumaDetail_l;
    pred_l  = saturate(pred_l);

    // Convert back to sRGB, write RGBA (swizzle .bgr → R,G,B)
    float3 pred_s = sGam(pred_l);
    tOut[pos] = float4(pred_s.bgr, 1.0);
}
)HLSL";

/* ============================================================================
   Display shaders
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

// IDXGIAdapter3 for VRAM telemetry (DXGI 1.4 / Windows 10; may be null)
static ComPtr<IDXGIAdapter3>          g_adapter3;

// Frame textures + SRVs
static ComPtr<ID3D11Texture2D>            g_texF0, g_texF1, g_texF2;
static ComPtr<ID3D11ShaderResourceView>   g_srvF0, g_srvF1, g_srvF2;

// LK output
static ComPtr<ID3D11Texture2D>            g_texFlow, g_texQual;
static ComPtr<ID3D11ShaderResourceView>   g_srvFlow, g_srvQual;
static ComPtr<ID3D11UnorderedAccessView>  g_uavFlow, g_uavQual;

// Prediction texture (RGBA_UNORM)
static ComPtr<ID3D11Texture2D>            g_texPred;
static ComPtr<ID3D11ShaderResourceView>   g_srvPred;
static ComPtr<ID3D11UnorderedAccessView>  g_uavPred;

// Shaders
static ComPtr<ID3D11ComputeShader>  g_csLK;
static ComPtr<ID3D11ComputeShader>  g_csSov;
static ComPtr<ID3D11VertexShader>   g_vsDisp;
static ComPtr<ID3D11PixelShader>    g_psDisp;

// Constant buffers
static ComPtr<ID3D11Buffer>  g_cbLK;
static ComPtr<ID3D11Buffer>  g_cbSov;

// Samplers
static ComPtr<ID3D11SamplerState>  g_samplerBilin;
static ComPtr<ID3D11SamplerState>  g_samplerPoint;

// Timing / state
static double g_captureMs = 33.33;
static double g_tau       = 0.5;

// Control
static HWND              g_hwnd        = nullptr;   // hotkey message window
static HWND              g_overlayHwnd = nullptr;   // overlay window (global) [v3]
static std::atomic<bool> g_quit       {false};
static const int HOTKEY_ID = 42;

/* ============================================================================
   Error macros
   ============================================================================ */
#define CHECK_HR(hr, msg) do { if (FAILED(hr)) { \
    fprintf(stderr, "[KRONOS] %s  hr=%08X\n", msg, (unsigned)(hr)); \
    ExitProcess(1); } } while(0)

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
   CreateTex helper
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
   InitD3D — device, hotkey window
   ============================================================================ */
static void InitD3D()
{
    WNDCLASSEXA wc{sizeof(wc)};
    wc.lpfnWndProc   = DefWindowProcA;
    wc.lpszClassName = "KRONOSHotkey";
    wc.hInstance     = GetModuleHandleA(nullptr);
    RegisterClassExA(&wc);
    g_hwnd = CreateWindowExA(0, "KRONOSHotkey", nullptr, 0, 0,0,0,0,
                              HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    RegisterHotKey(g_hwnd, HOTKEY_ID, MOD_CONTROL | MOD_SHIFT, 'X');

    ComPtr<IDXGIFactory1> factory;
    CHECK_HR(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)factory.GetAddressOf()), "CreateDXGIFactory1");
    ComPtr<IDXGIAdapter1> adapter;
    factory->EnumAdapters1(0, adapter.GetAddressOf());

    // Try IDXGIAdapter3 for VRAM telemetry (DXGI 1.4 / Windows 10)
    adapter.As(&g_adapter3);  // silently null on older systems

    D3D_FEATURE_LEVEL featureLevel;
    D3D_FEATURE_LEVEL requested[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    CHECK_HR(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                               0, requested, 2, D3D11_SDK_VERSION,
                               g_dev.GetAddressOf(), &featureLevel, g_ctx.GetAddressOf()),
             "D3D11CreateDevice");

    printf("  %s[D3D11]%s  Feature level %X   Adapter: ", C_GREEN, A_RESET, featureLevel);
    DXGI_ADAPTER_DESC1 ad{}; if (adapter) adapter->GetDesc1(&ad);
    printf("%ls", ad.Description);
    if (g_adapter3) printf("  %s[VRAM OK]%s", C_GREEN, A_RESET);
    printf("\n"); fflush(stdout);
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

    CHECK_HR(g_dev.As(&dxgiDev),                              "QueryInterface IDXGIDevice");
    CHECK_HR(dxgiDev->GetAdapter(dxgiAdp.GetAddressOf()),     "GetAdapter");
    CHECK_HR(dxgiAdp->EnumOutputs(0, dxgiOut.GetAddressOf()), "EnumOutputs");
    CHECK_HR(dxgiOut.As(&dxgiOut1),                           "QueryInterface IDXGIOutput1");
    CHECK_HR(dxgiOut1->DuplicateOutput(g_dev.Get(), g_dup.GetAddressOf()), "DuplicateOutput");

    DXGI_OUTPUT_DESC od{}; dxgiOut->GetDesc(&od);
    g_width  = od.DesktopCoordinates.right  - od.DesktopCoordinates.left;
    g_height = od.DesktopCoordinates.bottom - od.DesktopCoordinates.top;

    printf("  %s[DXGI]%s   Desktop %u x %u  (duplication active)\n",
           C_GREEN, A_RESET, g_width, g_height); fflush(stdout);
}

/* ============================================================================
   InitPipeline — textures, shaders, CBs, samplers, swap chain + overlay
   ============================================================================ */
static void InitPipeline()
{
    ComPtr<IDXGIDevice>  dxgiDev; g_dev.As(&dxgiDev);
    ComPtr<IDXGIAdapter> dxgiAdp; dxgiDev->GetAdapter(dxgiAdp.GetAddressOf());
    ComPtr<IDXGIFactory> dxgiFactory;
    dxgiAdp->GetParent(__uuidof(IDXGIFactory), (void**)dxgiFactory.GetAddressOf());

    /* ── Overlay window ────────────────────────────────────────────────────
       [BUG-11] WS_EX_TRANSPARENT — mouse/keyboard pass through to game.
       [BUG-15] SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) — prevents
                DXGI Desktop Duplication from capturing KRONOS's own output
                frames, which would create a feedback loop where the engine
                computes optical flow on its own rendering artefacts.
       g_overlayHwnd stored globally [v3] for proper lifecycle management.  */
    WNDCLASSEXA owc{sizeof(owc)};
    owc.lpfnWndProc   = DefWindowProcA;
    owc.lpszClassName = "KRONOSOverlay";
    owc.hInstance     = GetModuleHandleA(nullptr);
    owc.hbrBackground = nullptr;
    owc.style         = CS_OWNDC;
    RegisterClassExA(&owc);

    g_overlayHwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,  // [BUG-11]
        "KRONOSOverlay", "KRONOS",
        WS_POPUP | WS_VISIBLE,
        0, 0, g_width, g_height,
        nullptr, nullptr, owc.hInstance, nullptr);
    SetLayeredWindowAttributes(g_overlayHwnd, 0, 255, LWA_ALPHA);

    /* FIX [BUG-15]: Exclude overlay from Desktop Duplication capture.
       WDA_EXCLUDEFROMCAPTURE = 0x00000011 (Windows 10 build 19041+).
       On older systems the call is silently skipped — DuplicateOutput will
       still work, but the self-capture feedback loop will be present.
       Log which path was taken so the user knows.                          */
    {
        const DWORD WDA_EXCLUDEFROMCAPTURE_VAL = 0x00000011u;
        typedef BOOL (WINAPI *PFNSWDA)(HWND, DWORD);
        HMODULE u32 = GetModuleHandleA("user32.dll");
        bool excluded = false;
        if (u32) {
            auto fn = (PFNSWDA)GetProcAddress(u32, "SetWindowDisplayAffinity");
            if (fn) excluded = !!fn(g_overlayHwnd, WDA_EXCLUDEFROMCAPTURE_VAL);
        }
        printf("  %s[OVERLAY]%s  WDA_EXCLUDEFROMCAPTURE: %s%s%s  (feedback loop: %s%s%s)\n",
               C_GREEN, A_RESET,
               excluded ? C_GREEN : C_ORANGE, excluded ? "ACTIVE" : "UNAVAILABLE (Win<10.2004)", A_RESET,
               excluded ? C_GREEN : C_ORANGE, excluded ? "CLOSED" : "PRESENT", A_RESET);
        fflush(stdout);
    }

    /* ── Swap chain ────────────────────────────────────────────────────── */
    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferDesc.Width  = g_width;
    scd.BufferDesc.Height = g_height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc        = {1, 0};
    scd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount       = 2;
    scd.OutputWindow      = g_overlayHwnd;
    scd.Windowed          = TRUE;
    scd.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;
    CHECK_HR(dxgiFactory->CreateSwapChain(g_dev.Get(), &scd, g_sc.GetAddressOf()), "CreateSwapChain");

    // [BUG-12] Prevent DXGI from intercepting Alt+Enter
    dxgiFactory->MakeWindowAssociation(g_overlayHwnd, DXGI_MWA_NO_ALT_ENTER);

    ComPtr<ID3D11Texture2D> backbuf;
    g_sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backbuf.GetAddressOf());
    CHECK_HR(g_dev->CreateRenderTargetView(backbuf.Get(), nullptr, g_rtv.GetAddressOf()), "CreateRTV");

    /* ── Textures ──────────────────────────────────────────────────────── */
    UINT bindSRV = D3D11_BIND_SHADER_RESOURCE;
    UINT bindRW  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    CreateTex(g_width, g_height, DXGI_FORMAT_B8G8R8A8_UNORM, bindSRV,
              g_texF0.GetAddressOf(), g_srvF0.GetAddressOf(), nullptr);
    CreateTex(g_width, g_height, DXGI_FORMAT_B8G8R8A8_UNORM, bindSRV,
              g_texF1.GetAddressOf(), g_srvF1.GetAddressOf(), nullptr);
    CreateTex(g_width, g_height, DXGI_FORMAT_B8G8R8A8_UNORM, bindSRV,
              g_texF2.GetAddressOf(), g_srvF2.GetAddressOf(), nullptr);

    CreateTex(g_width, g_height, DXGI_FORMAT_R32G32_FLOAT, bindRW,
              g_texFlow.GetAddressOf(), g_srvFlow.GetAddressOf(), g_uavFlow.GetAddressOf());
    CreateTex(g_width, g_height, DXGI_FORMAT_R32_FLOAT, bindRW,
              g_texQual.GetAddressOf(), g_srvQual.GetAddressOf(), g_uavQual.GetAddressOf());

    CreateTex(g_width, g_height, DXGI_FORMAT_R8G8B8A8_UNORM, bindRW,
              g_texPred.GetAddressOf(), g_srvPred.GetAddressOf(), g_uavPred.GetAddressOf());

    /* ── Shaders ───────────────────────────────────────────────────────── */
    CompileCS(kLKFlowCS,    "CSMain", g_csLK);
    CompileCS(kSovereignCS, "CSMain", g_csSov);
    CompileVS(kDispVS,  g_vsDisp);
    CompilePS(kDispPS,  g_psDisp);

    /* ── Constant buffers ──────────────────────────────────────────────── */
    auto MakeCB = [&](UINT sz, ComPtr<ID3D11Buffer>& cb) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth      = sz;
        bd.Usage          = D3D11_USAGE_DYNAMIC;
        bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        CHECK_HR(g_dev->CreateBuffer(&bd, nullptr, cb.GetAddressOf()), "CreateCB");
    };
    MakeCB(16, g_cbLK);
    MakeCB(16, g_cbSov);

    /* ── Samplers ──────────────────────────────────────────────────────── */
    {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD         = 0;
        CHECK_HR(g_dev->CreateSamplerState(&sd, g_samplerBilin.GetAddressOf()), "CreateSamplerBilin");
    }
    {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter         = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD         = 0;
        CHECK_HR(g_dev->CreateSamplerState(&sd, g_samplerPoint.GetAddressOf()), "CreateSamplerPoint");
    }

    printf("  %s[PIPELINE]%s  Textures, shaders, CBs ready.\n", C_GREEN, A_RESET);
    fflush(stdout);
}

/* ============================================================================
   CaptureFrame — DXGI blocking acquire
   ============================================================================
   FIX [BUG-10] v2: DuplicateOutput re-init now has HRESULT check.
   FIX [BUG-17] v3: Verify acquired surface format and dimensions before
     CopyResource.  On HDR monitors (R16G16B16A16_FLOAT) or BGRX surfaces
     the copy would silently fail with E_INVALIDARG, leaving F2 black and
     causing the engine to compute flow on black frames — zero flow everywhere,
     synthesis falls back to F2 (black) permanently.
   ============================================================================ */
enum CaptureStatus { CS_NEW, CS_DUPLICATE, CS_TIMEOUT };

static CaptureStatus CaptureFrame(UINT timeoutMs)
{
    ComPtr<IDXGIResource>   res;
    DXGI_OUTDUPL_FRAME_INFO info{};
    HRESULT hr = g_dup->AcquireNextFrame(timeoutMs, &info, res.GetAddressOf());
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return CS_TIMEOUT;
    if (FAILED(hr)) {
        // Desktop dup lost — reinit (FIX [BUG-10] v2: HRESULT checked)
        g_dup.Reset();
        ComPtr<IDXGIDevice>  dxgiDev; g_dev.As(&dxgiDev);
        ComPtr<IDXGIAdapter> dxgiAdp; dxgiDev->GetAdapter(dxgiAdp.GetAddressOf());
        ComPtr<IDXGIOutput>  dxgiOut; dxgiAdp->EnumOutputs(0, dxgiOut.GetAddressOf());
        ComPtr<IDXGIOutput1> dxgiOut1;
        if (SUCCEEDED(dxgiOut.As(&dxgiOut1))) {
            HRESULT hrDup = dxgiOut1->DuplicateOutput(g_dev.Get(), g_dup.GetAddressOf());
            if (FAILED(hrDup))
                fprintf(stderr, "[KRONOS] Desktop Dup re-init failed hr=%08X — retrying.\n", (unsigned)hrDup);
        }
        return CS_TIMEOUT;
    }

    bool changed = (info.LastPresentTime.QuadPart != 0);
    if (changed) {
        ComPtr<ID3D11Texture2D> surf;
        if (SUCCEEDED(res.As(&surf))) {
            /* FIX [BUG-17]: Verify format + dimensions before CopyResource.
               CopyResource is documented to fail (E_INVALIDARG) silently if
               the source and destination descriptors don't match exactly.   */
            D3D11_TEXTURE2D_DESC sd{};
            surf->GetDesc(&sd);
            if (sd.Format == DXGI_FORMAT_B8G8R8A8_UNORM &&
                sd.Width  == g_width &&
                sd.Height == g_height) {
                g_ctx->CopyResource(g_texF2.Get(), surf.Get());
            } else {
                fprintf(stderr,
                    "[KRONOS][BUG-17] Surface mismatch: fmt=%u %ux%u (expected BGRA %ux%u). "
                    "Frame skipped. Check monitor HDR / scaling settings.\n",
                    sd.Format, sd.Width, sd.Height, g_width, g_height);
                g_dup->ReleaseFrame();
                return CS_TIMEOUT;
            }
        }
    }

    g_dup->ReleaseFrame();
    return changed ? CS_NEW : CS_DUPLICATE;
}

/* ============================================================================
   RunLKFlow — Dispatch Pass 1
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
   RunSovereign — Dispatch Pass 2: GFF synthesis
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
   Display — fullscreen triangle + Present
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
    g_sc->Present(0, 0);
}

/* ============================================================================
   RunLoop — main capture / generate / display loop
   ============================================================================
   FIX [BUG-01] v2: halfMs = frameMs / 2.0 (was = frameMs, halving output fps).

   FIX [BUG-13] v3: tau formula corrected.
     v2: g_tau = halfMs / g_captureMs
         At steady state: halfMs = captureMs/4  →  tau = 0.25.
         FLGE/FASW extrapolated only ¼ of the way to the next frame.
         Synthesised frames clustered near F2, making motion jerky.
     v3: g_tau = 2.0 * halfMs / g_captureMs
         At steady state: 2 * captureMs/4 / captureMs = 0.50. ✓
         The synthesised frame is placed exactly halfway between F2 and F3,
         producing smooth, evenly-spaced motion as intended.
   ============================================================================ */
static void RunLoop(int targetFps)
{
    const double frameMs  = 1000.0 / targetFps;
    const double halfMs   = frameMs / 2.0;       // [BUG-01] v2
    const double emaAlpha = 0.10;

    // Wait for first frame
    while (!g_quit) {
        MSG msg; while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID) g_quit = true;
            DispatchMessageA(&msg);
        }
        if (CaptureFrame(100) == CS_NEW) break;
    }
    if (g_quit) return;

    // Prime F0 = F1 = F2 = first frame
    g_ctx->CopyResource(g_texF1.Get(), g_texF2.Get());
    g_ctx->CopyResource(g_texF0.Get(), g_texF2.Get());

    // Initial timing: capture at half the output rate
    g_captureMs = 1000.0 / (targetFps / 2.0);
    g_tau       = 0.5;

    RunLKFlow();
    RunSovereign();

    int    outCount = 0, capCount = 0, idleCount = 0;
    int    synthPredCount = 0, synthTotalCount = 0;
    auto   statTick    = Clock::now();
    auto   lastCapTime = Clock::now();
    auto   nextSlot    = Clock::now();
    bool   showReal    = true;
    CaptureStatus lastStatus = CS_NEW;

    // Latency tracking: time from when CaptureFrame returns CS_NEW
    // to when Display() is called for that frame's real slot.
    auto   captureReturnTime = Clock::now();
    double latAccumUs = 0.0;
    int    latCount   = 0;

    printf("\n  %s->%s  Running!  Ctrl+Shift+X to stop.\n", C_GOLD2, A_RESET);
    fflush(stdout);

    // VRAM snapshot
    UINT64 vramUsed = 0, vramBudget = 0;

    while (!g_quit)
    {
        MSG msg; while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID) g_quit = true;
            DispatchMessageA(&msg);
        }
        if (g_quit) break;

        if (showReal)
        {
            if (lastStatus == CS_NEW) {
                // FIX [BUG-13]: tau = 2*halfMs/captureMs → 0.50 at steady state
                g_tau = std::max(0.25, std::min(0.75, (2.0 * halfMs) / g_captureMs));

                RunLKFlow();
                RunSovereign();

                // Measure capture → display latency for this real frame
                auto displayNow = Clock::now();
                double latUs = std::chrono::duration_cast<std::chrono::microseconds>(
                                   displayNow - captureReturnTime).count();
                latAccumUs += latUs;
                latCount++;
            } else {
                idleCount++;
            }
            Display(g_srvF2.Get());
            outCount++;
            showReal = false;
        }
        else
        {
            bool usedPred = (lastStatus == CS_NEW);
            if (usedPred)
                Display(g_srvPred.Get());
            else
                Display(g_srvF2.Get());
            outCount++;
            synthTotalCount++;
            if (usedPred) synthPredCount++;

            if (lastStatus == CS_NEW) {
                g_ctx->CopyResource(g_texF0.Get(), g_texF1.Get());
                g_ctx->CopyResource(g_texF1.Get(), g_texF2.Get());
            }

            auto now0     = Clock::now();
            auto slotEnd  = nextSlot + std::chrono::microseconds((long long)(halfMs * 1000.0));
            auto remainUs = std::chrono::duration_cast<std::chrono::microseconds>(slotEnd - now0).count();
            int  capTimeout = (remainUs > 2000) ? (int)(remainUs / 1000) : 2;

            lastStatus = CaptureFrame((UINT)capTimeout);

            if (lastStatus == CS_NEW) {
                capCount++;
                captureReturnTime = Clock::now();  // record when capture returned
                auto now1 = Clock::now();
                double measured = Dur(now1 - lastCapTime).count();
                lastCapTime = now1;
                if (measured > 1.0 && measured < 500.0)
                    g_captureMs = emaAlpha * measured + (1.0 - emaAlpha) * g_captureMs;
            }

            showReal = true;
        }

        // Microsecond-precise frame pacing
        nextSlot += std::chrono::microseconds((long long)(halfMs * 1000.0));
        {
            auto sleepUntil = nextSlot - std::chrono::microseconds(300);
            while (!g_quit && Clock::now() < sleepUntil)
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            while (!g_quit && Clock::now() < nextSlot)
                _mm_pause();
        }

        // Stats update every second
        double elapsed = Dur(Clock::now() - statTick).count();
        if (elapsed >= 1000.0) {
            g_fpsOut = outCount  / (elapsed / 1000.0);
            g_fpsCap = capCount  / (elapsed / 1000.0);
            bool idle = (idleCount > capCount);
            g_accuracy = (synthTotalCount > 0)
                         ? 100.0 * synthPredCount / synthTotalCount : 0.0;
            g_latencyUs = (latCount > 0) ? latAccumUs / latCount : 0.0;
            g_tau_cur   = std::max(0.25, std::min(0.75, (2.0 * halfMs) / g_captureMs));

            // VRAM query (IDXGIAdapter3, may be null on older systems)
            if (g_adapter3) {
                DXGI_QUERY_VIDEO_MEMORY_INFO vmi{};
                if (SUCCEEDED(g_adapter3->QueryVideoMemoryInfo(
                        0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vmi))) {
                    vramUsed   = vmi.CurrentUsage;
                    vramBudget = vmi.Budget;
                }
            }

            outCount = capCount = idleCount = 0;
            synthPredCount = synthTotalCount = 0;
            latAccumUs = 0.0; latCount = 0;
            statTick = Clock::now();

            PrintStats(g_fpsCap, g_fpsOut, g_captureMs, idle,
                       g_tau_cur, g_latencyUs, g_accuracy,
                       vramUsed, vramBudget);
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
    /* FIX [BUG-16]: DPI awareness must be set BEFORE any HWND creation.
       Without this, on 150 % DPI the overlay HWND used logical pixels,
       covering only ~44 % of the physical display area.                    */
    InitDpiAwareness();

    InitConsole();
    ShowSplash();

    EngineConfig cfg = ShowMenu();
    ShowRunning(cfg.fps);

    /* FIX [v3]: Check return values of priority/timer APIs.  */
    if (timeBeginPeriod(1) != TIMERR_NOERROR)
        fprintf(stderr, "[KRONOS] WARNING: timeBeginPeriod(1) failed — frame pacing may be imprecise.\n");

    if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS))
        fprintf(stderr, "[KRONOS] WARNING: HIGH_PRIORITY_CLASS failed (needs Admin?) — scheduling may be suboptimal.\n");

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    InitD3D();
    InitDup();
    InitPipeline();

    RunLoop(cfg.fps);

    UnregisterHotKey(g_hwnd, HOTKEY_ID);
    timeEndPeriod(1);
    return 0;
}

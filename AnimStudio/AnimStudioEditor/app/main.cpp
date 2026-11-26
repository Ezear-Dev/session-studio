// AnimStudio Editor -- entry point: Win32 window, Direct3D 11, Dear ImGui loop (from ImGui's
// example_win32_directx11, MIT licence). Option --selftest: report on the files, then exit.
#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include <string>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "app.h"
#include "workspace.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
bool g_occluded = false;
UINT g_resizeW = 0, g_resizeH = 0;
App* g_app = nullptr;

void CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) { g_dev->CreateRenderTargetView(back, nullptr, &g_rtv); back->Release(); }
}
void CleanupRenderTarget() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL levels[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &level, &g_ctx);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &level, &g_ctx);
    if (res != S_OK) return false;
    CreateRenderTarget();
    return true;
}
void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) return 0;
            g_resizeW = (UINT)LOWORD(lParam);
            g_resizeH = (UINT)HIWORD(lParam);
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;   // no Alt menu
            break;
        case WM_CLOSE:
            if (g_app) { g_app->RequestClose(); return 0; }   // the application asks for confirmation if needed
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

std::wstring ExeDir() {
    wchar_t p[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, p, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    wchar_t* s = wcsrchr(p, L'\\');
    if (s) s[1] = 0;
    return p;
}
} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmd, int) {
    const bool selftest = cmd && wcsstr(cmd, L"--selftest");
    if (selftest) {
        // no window: report on the parent console (or selftest.txt next to the exe)
        App app(ExeDir());
        std::string report;
        const int code = app.SelfTest(report);
        if (AttachConsole(ATTACH_PARENT_PROCESS)) { FILE* f = nullptr; freopen_s(&f, "CONOUT$", "w", stdout); std::fputs(report.c_str(), stdout); std::fflush(stdout); }
        FILE* f = _wfsopen((ExeDir() + L"selftest.txt").c_str(), L"wb", _SH_DENYNO);
        if (f) { std::fputs(report.c_str(), f); std::fclose(f); }
        return code;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);   // UI Phase 1: Windows folder picker
    ImGui_ImplWin32_EnableDpiAwareness();
    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInst, nullptr, LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr, L"AnimStudioEditor", nullptr};
    RegisterClassExW(&wc);
    // UI Phase 1: the window comes back where it was (if that place is still on a screen)
    const std::wstring ini = ExeDir() + L"AnimStudioEditor.ini";
    int wx = 80, wy = 60, ww = (int)(1600 * scale), wh = (int)(900 * scale);
    bool wmax = false;
    {
        Settings st;
        LoadSettings(ini, st);
        const int x = (int)st.GetFloat("window.x", -99999.f), y = (int)st.GetFloat("window.y", -99999.f);
        const int w = (int)st.GetFloat("window.w", 0.f), h = (int)st.GetFloat("window.h", 0.f);
        const RECT r{x, y, x + w, y + h};
        if (w >= 400 && h >= 300 && MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) { wx = x; wy = y; ww = w; wh = h; }
        wmax = st.Get("window.max") == "1";
    }
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"AnimStudio Editor", WS_OVERLAPPEDWINDOW, wx, wy, ww, wh, nullptr, nullptr, hInst, nullptr);
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, hInst);
        MessageBoxW(nullptr, L"Direct3D 11 is unavailable: the editor cannot start.", L"AnimStudio Editor", MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, wmax ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;   // ImGui's own file stays off: panel sizes live in AnimStudioEditor.ini (UI Phase 1)
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding = 3.f;
    style.GrabRounding = 3.f;
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    // font with accents (Windows Segoe UI), else ImGui's one
    if (GetFileAttributesW(L"C:\\Windows\\Fonts\\segoeui.ttf") != INVALID_FILE_ATTRIBUTES) io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17.f);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    App app(ExeDir());
    g_app = &app;
    LARGE_INTEGER freq, last, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (app.ShouldQuit()) {
            WINDOWPLACEMENT wp{sizeof(wp)};
            if (GetWindowPlacement(hwnd, &wp)) {
                Settings st;
                LoadSettings(ini, st);
                const RECT& r = wp.rcNormalPosition;
                st.SetFloat("window.x", (float)r.left); st.SetFloat("window.y", (float)r.top);
                st.SetFloat("window.w", (float)(r.right - r.left)); st.SetFloat("window.h", (float)(r.bottom - r.top));
                st.Set("window.max", wp.showCmd == SW_SHOWMAXIMIZED ? "1" : "0");
                SaveSettings(ini, st);
            }
            DestroyWindow(hwnd);
            continue;
        }
        if (g_occluded && g_swap->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) { Sleep(10); continue; }
        g_occluded = false;
        if (g_resizeW != 0 && g_resizeH != 0) {
            CleanupRenderTarget();
            g_swap->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            CreateRenderTarget();
        }
        QueryPerformanceCounter(&now);
        float dt = (float)((double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart);
        last = now;
        if (dt > 0.1f) dt = 0.1f;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app.Frame(dt);
        ImGui::Render();
        const float clear[4] = {0.09f, 0.10f, 0.12f, 1.f};
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const HRESULT hr = g_swap->Present(1, 0);
        g_occluded = (hr == DXGI_STATUS_OCCLUDED);
    }
    g_app = nullptr;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    UnregisterClassW(wc.lpszClassName, hInst);
    return 0;
}

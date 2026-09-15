#include "gui.h"

#include <d3d11.h>

#include "vendor/imgui/imgui.h"
#include "vendor/imgui/imgui_impl_win32.h"
#include "vendor/imgui/imgui_impl_dx11.h"

#pragma comment(lib, "d3d11.lib")

static ID3D11Device *g_device = nullptr;
static ID3D11DeviceContext *g_context = nullptr;
static IDXGISwapChain *g_swap_chain = nullptr;
static ID3D11RenderTargetView *g_rtv = nullptr;
static HWND g_hwnd = nullptr;
static int g_width = 0;
static int g_height = 0;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
        return true;

    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static bool create_overlay_window() {
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "OverlayClass";
    RegisterClassEx(&wc);

    g_width  = GetSystemMetrics(SM_CXSCREEN);
    g_height = GetSystemMetrics(SM_CYSCREEN);

    g_hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        wc.lpszClassName, "",
        WS_POPUP,
        0, 0, g_width, g_height,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!g_hwnd) return false;

    SetLayeredWindowAttributes(g_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(g_hwnd);
    return true;
}

static bool create_device() {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = g_width;
    sd.BufferDesc.Height = g_height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 0;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL fl;
    if (FAILED(D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &sd, &g_swap_chain, &g_device, &fl, &g_context)))
        return false;

    ID3D11Texture2D* back_buf = nullptr;
    g_swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buf));
    g_device->CreateRenderTargetView(back_buf, nullptr, &g_rtv);
    back_buf->Release();
    return true;
}

static bool world_to_screen(const vec3& world, const view_matrix_t& vm,
                            int screen_w, int screen_h, vec2& out) {
    float w = vm.m[3][0]*world.x + vm.m[3][1]*world.y + vm.m[3][2]*world.z + vm.m[3][3];
    if (w < 0.1f) return false;

    float x = vm.m[0][0]*world.x + vm.m[0][1]*world.y + vm.m[0][2]*world.z + vm.m[0][3];
    float y = vm.m[1][0]*world.x + vm.m[1][1]*world.y + vm.m[1][2]*world.z + vm.m[1][3];

    out.x = (screen_w / 2.0f) + (x / w) * (screen_w / 2.0f);
    out.y = (screen_h / 2.0f) - (y / w) * (screen_h / 2.0f);
    return true;
}

bool overlay::init() {
    if (!create_overlay_window()) return false;
    if (!create_device()) return false;

    ImGui::CreateContext();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);
    return true;
}

void overlay::run(SharedState& state) {
    MSG msg = {};
    while (state.running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                state.running = false;
                return;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        std::vector<EnemyData> enemies;
        view_matrix_t vm;
        {
            std::lock_guard<std::mutex> lock(state.mtx);
            enemies = state.enemies;
            vm = state.view_matrix;
        }

        ImDrawList* draw = ImGui::GetBackgroundDrawList();

        char dbg[64];
        snprintf(dbg, sizeof(dbg), "enemies: %d", (int)enemies.size());
        draw->AddText(ImVec2(10, 10), IM_COL32(0, 255, 0, 255), dbg);

        for (const auto& e : enemies) {
            vec2 feet_scr, head_scr;
            if (!world_to_screen(e.feet, vm, g_width, g_height, feet_scr)) continue;
            if (!world_to_screen(e.head, vm, g_width, g_height, head_scr)) continue;

            float box_h = feet_scr.y - head_scr.y;
            float box_w = box_h * 0.45f;

            ImVec2 tl(head_scr.x - box_w / 2, head_scr.y);
            ImVec2 br(head_scr.x + box_w / 2, feet_scr.y);
            draw->AddRect(tl, br, IM_COL32(255, 255, 255, 255), 0.0f, 0, 1.5f);
        }

        ImGui::Render();

        const float clear[4] = { 0.f, 0.f, 0.f, 0.f };
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap_chain->Present(1, 0);
    }
}

void overlay::shutdown() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (g_rtv)        g_rtv->Release();
    if (g_swap_chain) g_swap_chain->Release();
    if (g_context)    g_context->Release();
    if (g_device)     g_device->Release();
    DestroyWindow(g_hwnd);
}

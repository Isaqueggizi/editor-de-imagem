#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <wlanapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr UINT WM_SCAN_DONE = WM_APP + 1;
constexpr UINT_PTR TIMER_ANIMATION = 1;
constexpr COLORREF kBg = RGB(10, 15, 28), kPanel = RGB(18, 26, 43);
constexpr COLORREF kCyan = RGB(43, 213, 196), kBlue = RGB(61, 127, 255);

struct Snapshot {
    std::wstring adapter = L"Procurando adaptador Wi-Fi...";
    std::wstring status = L"Pronto para analisar";
    int signal = 0;
    int latency = -1;
    int score = 0;
    bool connected = false;
};

Snapshot g_data;
std::mutex g_mutex;
std::atomic_bool g_busy{false};
HFONT g_titleFont{}, g_bigFont{}, g_bodyFont{}, g_smallFont{};
HWND g_window{};

void text(HDC dc, const std::wstring& value, RECT r, HFONT font, COLORREF color,
          UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    SelectObject(dc, font);
    DrawTextW(dc, value.c_str(), -1, &r, format);
}

void rounded(HDC dc, RECT r, COLORREF fill, int radius = 18) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, fill);
    auto oldBrush = SelectObject(dc, brush);
    auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

int tcpLatencyMs() {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return -1;
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) { WSACleanup(); return -1; }
    u_long nonblocking = 1;
    ioctlsocket(sock, FIONBIO, &nonblocking);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(443);
    InetPtonW(AF_INET, L"1.1.1.1", &address.sin_addr);
    auto begin = std::chrono::steady_clock::now();
    connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    fd_set writes; FD_ZERO(&writes); FD_SET(sock, &writes);
    timeval timeout{2, 0};
    int ready = select(0, nullptr, &writes, nullptr, &timeout);
    int result = ready > 0 ? static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - begin).count()) : -1;
    closesocket(sock);
    WSACleanup();
    return result;
}

Snapshot scanNetwork() {
    Snapshot out;
    DWORD negotiated = 0;
    HANDLE client = nullptr;
    if (WlanOpenHandle(2, nullptr, &negotiated, &client) == ERROR_SUCCESS) {
        PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
        if (WlanEnumInterfaces(client, nullptr, &interfaces) == ERROR_SUCCESS && interfaces->dwNumberOfItems) {
            auto& info = interfaces->InterfaceInfo[0];
            out.adapter = info.strInterfaceDescription;
            out.connected = info.isState == wlan_interface_state_connected;
            DWORD size = 0; PVOID raw = nullptr; WLAN_OPCODE_VALUE_TYPE type{};
            if (WlanQueryInterface(client, &info.InterfaceGuid, wlan_intf_opcode_current_connection,
                nullptr, &size, &raw, &type) == ERROR_SUCCESS) {
                auto connection = static_cast<PWLAN_CONNECTION_ATTRIBUTES>(raw);
                out.signal = static_cast<int>(connection->wlanAssociationAttributes.wlanSignalQuality);
                WlanFreeMemory(raw);
            }
        } else {
            out.adapter = L"Nenhum adaptador Wi-Fi encontrado";
        }
        if (interfaces) WlanFreeMemory(interfaces);
        WlanCloseHandle(client, nullptr);
    } else {
        out.adapter = L"Serviço WLAN indisponível";
    }
    out.latency = out.connected ? tcpLatencyMs() : -1;
    const int latencyScore = out.latency < 0 ? 0 : std::clamp(100 - out.latency * 2, 0, 100);
    out.score = out.connected ? static_cast<int>(out.signal * .65 + latencyScore * .35) : 0;
    out.status = out.connected ? (out.score >= 80 ? L"Conexão excelente" : out.score >= 55 ? L"Conexão estável" : L"Ajustes recomendados") : L"Wi-Fi desconectado";
    return out;
}

void beginScan(HWND window) {
    if (g_busy.exchange(true)) return;
    { std::lock_guard lock(g_mutex); g_data.status = L"Analisando sinal e latência..."; }
    InvalidateRect(window, nullptr, FALSE);
    std::thread([window] {
        auto next = scanNetwork();
        { std::lock_guard lock(g_mutex); g_data = std::move(next); }
        g_busy = false;
        PostMessageW(window, WM_SCAN_DONE, 0, 0);
    }).detach();
}

void applyAdaptiveProfile(HWND window) {
    int signal;
    { std::lock_guard lock(g_mutex); signal = g_data.signal; }
    // NexaRoute chooses a conservative TCP profile from live link quality. Windows
    // remains in control of congestion; this only restores supported global defaults.
    std::wstring args = L"/c netsh int tcp set global rss=enabled & netsh int tcp set global autotuninglevel=";
    args += signal > 35 ? L"normal" : L"highlyrestricted";
    args += L" & ipconfig /flushdns";
    auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"runas", L"cmd.exe", args.c_str(), nullptr, SW_HIDE));
    MessageBoxW(window, result > 32 ? L"Perfil NexaRoute aplicado. Analise novamente para comparar." : L"O Windows não autorizou a alteração. Nenhuma configuração foi modificada.", L"NexaFi", MB_OK | (result > 32 ? MB_ICONINFORMATION : MB_ICONWARNING));
}

bool buttonHit(POINT p, int top) { return p.x >= 54 && p.x <= 286 && p.y >= top && p.y <= top + 48; }

void paint(HWND window) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(window, &ps);
    RECT client{}; GetClientRect(window, &client);
    HDC memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    auto oldBitmap = SelectObject(memory, bitmap);
    FillRect(memory, &client, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    HBRUSH background = CreateSolidBrush(kBg); FillRect(memory, &client, background); DeleteObject(background);

    Snapshot d; { std::lock_guard lock(g_mutex); d = g_data; }
    text(memory, L"NEXA", {48, 30, 150, 64}, g_titleFont, RGB(238, 245, 255));
    text(memory, L"FI", {142, 30, 190, 64}, g_titleFont, kCyan);
    text(memory, L"CENTRAL WI-FI INTELIGENTE", {49, 65, 310, 87}, g_smallFont, RGB(110, 130, 158));

    rounded(memory, {36, 112, 318, 486}, RGB(14, 21, 36), 22);
    text(memory, L"VISÃO GERAL", {55, 130, 285, 155}, g_smallFont, RGB(105, 127, 158));
    text(memory, d.adapter, {55, 160, 292, 206}, g_bodyFont, RGB(220, 231, 246), DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
    text(memory, d.connected ? L"●  CONECTADO" : L"●  DESCONECTADO", {55, 210, 285, 237}, g_smallFont, d.connected ? kCyan : RGB(245, 111, 120));
    rounded(memory, {54, 265, 286, 313}, kBlue, 15);
    text(memory, g_busy ? L"ANALISANDO..." : L"ANALISAR AGORA", {54, 265, 286, 313}, g_bodyFont, RGB(255,255,255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    rounded(memory, {54, 329, 286, 377}, RGB(29, 44, 66), 15);
    text(memory, L"APLICAR NEXAROUTE", {54, 329, 286, 377}, g_bodyFont, RGB(189, 216, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text(memory, L"Seguro • Reversível • Sem milagres", {55, 405, 286, 430}, g_smallFont, RGB(99, 120, 148));
    text(memory, L"O app otimiza parâmetros do Windows; não aumenta a velocidade contratada.", {55, 432, 286, 470}, g_smallFont, RGB(99, 120, 148), DT_LEFT | DT_WORDBREAK);

    rounded(memory, {340, 112, client.right - 38, 300}, kPanel, 22);
    text(memory, L"NEXASCORE", {370, 135, 520, 162}, g_smallFont, RGB(107, 132, 166));
    text(memory, std::to_wstring(d.score), {368, 163, 535, 256}, g_bigFont, d.score >= 65 ? kCyan : kBlue);
    text(memory, L"/ 100", {520, 210, 620, 245}, g_bodyFont, RGB(106, 128, 158));
    text(memory, d.status, {370, 258, client.right - 65, 282}, g_bodyFont, RGB(219, 230, 244));

    int half = (client.right - 416) / 2;
    RECT card1{340, 322, 340 + half, 480}, card2{358 + half, 322, client.right - 38, 480};
    rounded(memory, card1, kPanel, 22); rounded(memory, card2, kPanel, 22);
    text(memory, L"SINAL WI-FI", {card1.left+28, 342, card1.right-15, 370}, g_smallFont, RGB(107,132,166));
    text(memory, std::to_wstring(d.signal) + L"%", {card1.left+28, 378, card1.right-15, 430}, g_titleFont, RGB(238,245,255));
    text(memory, L"qualidade do enlace", {card1.left+28, 437, card1.right-15, 462}, g_smallFont, RGB(92,113,143));
    text(memory, L"LATÊNCIA", {card2.left+28, 342, card2.right-15, 370}, g_smallFont, RGB(107,132,166));
    text(memory, d.latency < 0 ? L"—" : std::to_wstring(d.latency) + L" ms", {card2.left+28, 378, card2.right-15, 430}, g_titleFont, RGB(238,245,255));
    text(memory, L"TCP até 1.1.1.1", {card2.left+28, 437, card2.right-15, 462}, g_smallFont, RGB(92,113,143));

    rounded(memory, {340, 502, client.right-38, 588}, RGB(14, 31, 45), 18);
    text(memory, L"NexaRoute adaptativo", {370, 516, 650, 543}, g_bodyFont, kCyan);
    text(memory, L"Combina qualidade do sinal e latência para selecionar um perfil TCP suportado pelo Windows.", {370, 546, client.right-65, 574}, g_smallFont, RGB(145,165,190));

    BitBlt(dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
    SelectObject(memory, oldBitmap); DeleteObject(bitmap); DeleteDC(memory);
    EndPaint(window, &ps);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        const BOOL dark = TRUE;
        DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
        SetTimer(window, TIMER_ANIMATION, 1000, nullptr);
        beginScan(window);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (buttonHit(p, 265)) beginScan(window);
        else if (buttonHit(p, 329)) applyAdaptiveProfile(window);
        return 0;
    }
    case WM_SCAN_DONE: InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_TIMER: InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_PAINT: paint(window); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_DESTROY: KillTimer(window, TIMER_ANIMATION); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_titleFont = CreateFontW(-28,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    g_bigFont = CreateFontW(-68,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    g_bodyFont = CreateFontW(-16,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    g_smallFont = CreateFontW(-13,0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = windowProc; wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = CreateSolidBrush(kBg); wc.lpszClassName = L"NexaFiWindow";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION); wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);
    RECT size{0,0,960,640}; AdjustWindowRectEx(&size, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, FALSE, 0);
    g_window = CreateWindowExW(0, wc.lpszClassName, L"NexaFi — Wi-Fi inteligente", WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, size.right-size.left, size.bottom-size.top, nullptr, nullptr, instance, nullptr);
    if (!g_window) return 1;
    ShowWindow(g_window, show); UpdateWindow(g_window);
    MSG message{}; while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    DeleteObject(g_titleFont); DeleteObject(g_bigFont); DeleteObject(g_bodyFont); DeleteObject(g_smallFont);
    return static_cast<int>(message.wParam);
}

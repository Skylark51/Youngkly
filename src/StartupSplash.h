#pragma once

#include <windows.h>
#include <algorithm>
#include <thread>

// Independent of Qt so feedback appears even during first-run extraction.
class StartupSplash {
public:
    StartupSplash() {
        ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ready_)
            return;
        try {
            thread_ = std::thread([this] { run(); });
        } catch (...) {
            CloseHandle(ready_);
            ready_ = nullptr;
            return; // A cosmetic window must never prevent startup.
        }
        WaitForSingleObject(ready_, INFINITE);
    }
    ~StartupSplash() {
        if (window_)
            PostMessageW(window_, WM_CLOSE, 0, 0);
        if (thread_.joinable())
            thread_.join();
        if (ready_)
            CloseHandle(ready_);
    }
    void finish() {
        if (window_)
            PostMessageW(window_, WM_APP, 0, 0);
        if (thread_.joinable())
            thread_.join();
        window_ = nullptr;
    }

private:
    static constexpr ULONGLONG splashDurationMs = 3000;
    static constexpr ULONGLONG fadeDurationMs = 240;

    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
        auto self = reinterpret_cast<StartupSplash *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<StartupSplash *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self)
            return DefWindowProcW(window, message, w, l);
        switch (message) {
        case WM_APP:
            self->finished_ = true;
            return 0;
        case WM_TIMER: {
            const auto now = GetTickCount64();
            if (self->finished_ && now - self->started_ >= splashDurationMs - fadeDurationMs) {
                if (!self->fadeStarted_)
                    self->fadeStarted_ = now;
                const double t = std::min(1.0, (now - self->fadeStarted_) / static_cast<double>(fadeDurationMs));
                SetLayeredWindowAttributes(window, 0, static_cast<BYTE>(255 * (1 - t)), LWA_ALPHA);
                SetWindowPos(window, nullptr, self->x_, self->y_ + self->scale(22 * t * t),
                             0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                if (t >= 1) {
                    DestroyWindow(window);
                    return 0;
                }
            }
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(window, &paint);
            RECT area;
            GetClientRect(window, &area);
            HDC buffer = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, area.right, area.bottom);
            auto oldBitmap = SelectObject(buffer, bitmap);
            HBRUSH background = CreateSolidBrush(RGB(12, 24, 48));
            FillRect(buffer, &area, background);
            DeleteObject(background);
            const int iconSize = self->scale(140);
            if (self->icon_)
                DrawIconEx(buffer, (area.right - iconSize) / 2, self->scale(30),
                           self->icon_, iconSize, iconSize, 0, nullptr, DI_NORMAL);
            SetBkMode(buffer, TRANSPARENT);
            SetTextColor(buffer, RGB(244, 247, 253));
            auto oldFont = SelectObject(buffer, self->titleFont_);
            RECT title{0, self->scale(188), area.right, self->scale(228)};
            DrawTextW(buffer, L"Youngkly", -1, &title, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            SelectObject(buffer, self->bodyFont_);
            SetTextColor(buffer, RGB(183, 196, 216));
            RECT status{0, self->scale(238), area.right, self->scale(264)};
            DrawTextW(buffer, L"\uc2dc\uc791\ud558\ub294 \uc911", -1, &status, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            const int active = static_cast<int>((GetTickCount64() - self->started_) / 250 % 3);
            for (int i = 0; i < 3; ++i) {
                HBRUSH dot = CreateSolidBrush(i == active ? RGB(229, 193, 117) : RGB(64, 79, 104));
                RECT r{area.right / 2 + self->scale(i * 16 - 19), self->scale(282),
                       area.right / 2 + self->scale(i * 16 - 13), self->scale(286)};
                FillRect(buffer, &r, dot);
                DeleteObject(dot);
            }
            SelectObject(buffer, oldFont);
            BitBlt(dc, 0, 0, area.right, area.bottom, buffer, 0, 0, SRCCOPY);
            SelectObject(buffer, oldBitmap);
            DeleteObject(bitmap);
            DeleteDC(buffer);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, w, l);
    }
    int scale(double value) const { return static_cast<int>(value * dpi_ / 96.0); }
    void run() {
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        POINT cursor{};
        GetCursorPos(&cursor);
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor);
        HDC screen = GetDC(nullptr);
        dpi_ = GetDeviceCaps(screen, LOGPIXELSX);
        ReleaseDC(nullptr, screen);
        const auto instance = GetModuleHandleW(nullptr);
        WNDCLASSW cls{};
        cls.lpfnWndProc = procedure;
        cls.hInstance = instance;
        cls.lpszClassName = L"YoungklyStartupSplash";
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&cls);
        x_ = monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - scale(360)) / 2;
        y_ = monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - scale(320)) / 2;
        window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                  cls.lpszClassName, L"Youngkly - Starting", WS_POPUP,
                                  x_, y_, scale(360), scale(320), nullptr, nullptr, instance, this);
        if (window_) {
            dpi_ = GetDpiForWindow(window_);
            x_ = monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - scale(360)) / 2;
            y_ = monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - scale(320)) / 2;
            SetWindowPos(window_, nullptr, x_, y_, scale(360), scale(320), SWP_NOZORDER | SWP_NOACTIVATE);
            icon_ = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                                scale(140), scale(140), LR_DEFAULTCOLOR));
            titleFont_ = CreateFontW(-scale(30), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            bodyFont_ = CreateFontW(-scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Malgun Gothic");
            started_ = GetTickCount64();
            SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
            SetTimer(window_, 1, 16, nullptr);
            ShowWindow(window_, SW_SHOWNOACTIVATE);
            UpdateWindow(window_);
        }
        SetEvent(ready_);
        if (window_) {
            MSG message;
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (icon_) DestroyIcon(icon_);
        if (titleFont_) DeleteObject(titleFont_);
        if (bodyFont_) DeleteObject(bodyFont_);
    }
    HANDLE ready_ = nullptr;
    HWND window_ = nullptr;
    HICON icon_ = nullptr;
    HFONT titleFont_ = nullptr, bodyFont_ = nullptr;
    std::thread thread_;
    ULONGLONG started_ = 0, fadeStarted_ = 0;
    bool finished_ = false;
    int dpi_ = 96, x_ = 0, y_ = 0;
};

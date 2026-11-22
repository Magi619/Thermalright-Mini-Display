        dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = hwnd_; dialog.lpstrFilter = filter;
        dialog.lpstrFile = path; dialog.nMaxFile = MAX_PATH; dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
        dialog.lpstrTitle = L"Choose a GIF or image";
        if (!GetOpenFileNameW(&dialog)) return;
        StopPlayback();
        try {
            media_ = MediaAsset::Load(path);
            lastMediaPath_ = path;
            const auto slash = lastMediaPath_.find_last_of(L"\\/");
            const std::wstring name = slash == std::wstring::npos ? lastMediaPath_ : lastMediaPath_.substr(slash + 1);
            SetWindowTextW(mediaLabel_, (name + L"\r\n" + std::to_wstring(media_->frameCount) + (media_->animated ? L" GIF frames" : L" still image")).c_str());
            EnableWindow(startButton_, display_ != nullptr);
            SaveSettings(); RefreshPreview();
            SetStatus(L"Media loaded. Connect the BA120 and start playback.");
        } catch (const std::exception& ex) {
            media_.reset(); SetStatus(L"Unable to load media: " + Utf8ToWide(ex.what()));
            MessageBoxW(hwnd_, (L"Unable to load media:\r\n" + Utf8ToWide(ex.what())).c_str(), kAppName, MB_ICONWARNING | MB_OK);
        }
    }

    int ScaleMode() const { return static_cast<int>(SendMessageW(scaleCombo_, CB_GETCURSEL, 0, 0)); }
    int Rotation() const { return SendMessageW(rotationCombo_, CB_GETCURSEL, 0, 0) == 1 ? 270 : 90; }
    int MaximumFps() const { return std::clamp(static_cast<int>(SendMessageW(fpsTrack_, TBM_GETPOS, 0, 0)), 1, 12); }

    void RefreshPreview() {
        if (!media_) return;
        try {
            EncodedFrame frame = EncodeFrame(*media_, 0, ScaleMode(), Rotation());
            {
                std::lock_guard<std::mutex> lock(previewMutex_);
                previewPixels_ = std::move(frame.previewBgra);
            }
            InvalidateRect(preview_, nullptr, TRUE);
        } catch (const std::exception& ex) { SetStatus(L"Preview failed: " + Utf8ToWide(ex.what())); }
    }

    void StartPlayback() {
        if (playbackRunning_) return;
        if (!display_ || !media_) { SetStatus(L"Connect to the BA120 and choose an image or GIF first."); return; }
        if (playbackThread_.joinable()) playbackThread_.join();
        if (stopEvent_) CloseHandle(stopEvent_);
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stopEvent_) { SetStatus(L"Could not create the playback stop event."); return; }
        playbackRunning_ = true;
        EnableWindow(startButton_, FALSE); EnableWindow(stopButton_, TRUE);
        SaveSettings(); SetStatus(L"Starting native HID playback…");
        MediaAsset* media = media_.get();
        HidDisplay* display = display_.get();
        const int scale = ScaleMode(); const int rotation = Rotation(); const int fps = MaximumFps();
        HANDLE stop = stopEvent_;
        playbackThread_ = std::thread([this, media, display, scale, rotation, fps, stop]() {
            PlaybackLoop(*media, *display, scale, rotation, fps, stop);
        });
    }

    void PlaybackLoop(MediaAsset& media, HidDisplay& display, int scale, int rotation, int maxFps, HANDLE stop) {
        try {
            UINT frameIndex = 0;
            while (WaitForSingleObject(stop, 0) != WAIT_OBJECT_0) {
                EncodedFrame frame = EncodeFrame(media, frameIndex, scale, rotation);
                display.SendFrame(frame.reports, stop);
                {
                    std::lock_guard<std::mutex> lock(previewMutex_);
                    previewPixels_ = std::move(frame.previewBgra);
                }
                PostMessageW(hwnd_, WM_APP_PREVIEW, 0, 0);

                // The BA120 panel has a short display watchdog: in the user's hardware
                // test it reverted to the factory screen about 3 seconds after the last
                // HID transfer. Re-send the already encoded packet buffer every 2 seconds.
                // This does not decode or encode the image again; the worker sleeps on the
                // stop event between transfers, keeping idle CPU use very low.
                if (!media.animated) {
                    for (;;) {
                        const DWORD wait = WaitForSingleObject(stop, kKeepAliveIntervalMs);
                        if (wait == WAIT_OBJECT_0) break;
                        if (wait == WAIT_FAILED) throw std::runtime_error("Static keep-alive wait failed.");
                        display.SendFrame(frame.reports, stop);
                    }
                    break;
                }

                // GIF frames obey both the source frame delay and the selected FPS cap.
                // For long-duration frames, resend the same prepared packets every 2 s so
                // the panel does not time out. Advance to the next frame at its deadline.
                const UINT frameDuration = std::max<UINT>(frame.delayMs, static_cast<UINT>(1000 / maxFps));
                const ULONGLONG nextFrameAt = GetTickCount64() + frameDuration;
                bool stopRequested = false;
                for (;;) {
                    const ULONGLONG now = GetTickCount64();
                    if (now >= nextFrameAt) break;
                    const ULONGLONG remaining = nextFrameAt - now;
                    const DWORD waitMs = static_cast<DWORD>(std::min<ULONGLONG>(kKeepAliveIntervalMs, remaining));
                    const DWORD wait = WaitForSingleObject(stop, waitMs);
                    if (wait == WAIT_OBJECT_0) { stopRequested = true; break; }
                    if (wait == WAIT_FAILED) throw std::runtime_error("GIF frame wait failed.");
                    if (wait == WAIT_TIMEOUT && waitMs == kKeepAliveIntervalMs && GetTickCount64() < nextFrameAt)
                        display.SendFrame(frame.reports, stop);
                }
                if (stopRequested) break;
                frameIndex = (frameIndex + 1) % media.frameCount;
            }
        } catch (const std::exception& ex) {
            PostStatus(L"Playback stopped: " + Utf8ToWide(ex.what()));
        }
        playbackRunning_ = false;
        PostMessageW(hwnd_, WM_APP_PLAYBACK_STOPPED, 0, 0);
    }

    void StopPlayback() {
        if (stopEvent_) SetEvent(stopEvent_);
        if (playbackThread_.joinable()) playbackThread_.join();
        playbackRunning_ = false;
        if (stopEvent_) { CloseHandle(stopEvent_); stopEvent_ = nullptr; }
        if (stopButton_) EnableWindow(stopButton_, FALSE);
        if (startButton_) EnableWindow(startButton_, display_ && media_);
    }

    void AutoResume() {
        try {
            RefreshDevices();
            if (devices_.empty()) return;
            SendMessageW(deviceCombo_, CB_SETCURSEL, 0, 0);
            ConnectDevice();
            if (!lastMediaPath_.empty() && GetFileAttributesW(lastMediaPath_.c_str()) != INVALID_FILE_ATTRIBUTES) {
                media_ = MediaAsset::Load(lastMediaPath_);
                const auto slash = lastMediaPath_.find_last_of(L"\\/");
                const std::wstring name = slash == std::wstring::npos ? lastMediaPath_ : lastMediaPath_.substr(slash + 1);
                SetWindowTextW(mediaLabel_, (name + L"\r\n" + std::to_wstring(media_->frameCount) + L" frame(s)").c_str());
                StartPlayback();
            }
        } catch (const std::exception& ex) { PostStatus(L"Auto-resume failed: " + Utf8ToWide(ex.what())); }
    }

    void PaintPreview(const DRAWITEMSTRUCT* draw) {
        if (!draw || !draw->hDC) return;
        RECT area{};
        GetClientRect(preview_, &area);
        FillRect(draw->hDC, &area, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        std::lock_guard<std::mutex> lock(previewMutex_);
        if (previewPixels_.size() != static_cast<size_t>(kLogicalWidth * kLogicalHeight * 4)) {
            SetTextColor(draw->hDC, RGB(190, 190, 190));
            SetBkMode(draw->hDC, TRANSPARENT);
            DrawTextW(draw->hDC, L"Choose an image to preview", -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return;
        }
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = kLogicalWidth;
        bmi.bmiHeader.biHeight = -kLogicalHeight;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        const int width = area.right - area.left - 12;
        const int height = area.bottom - area.top - 12;
        StretchDIBits(draw->hDC, 6, 6, width, height, 0, 0,
            kLogicalWidth, kLogicalHeight, previewPixels_.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    }

    void AddTrayIcon() {
        ZeroMemory(&tray_, sizeof(tray_));
        tray_.cbSize = sizeof(tray_); tray_.hWnd = hwnd_; tray_.uID = kTrayId;
        tray_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; tray_.uCallbackMessage = WM_APP_TRAY;
        // Use the same embedded Windows Shell32 monitor-and-keyboard icon in the tray.
        tray_.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
        wcscpy_s(tray_.szTip, kAppName);
        Shell_NotifyIconW(NIM_ADD, &tray_);
    }
    void RemoveTrayIcon() { Shell_NotifyIconW(NIM_DELETE, &tray_); }

    void ShowMainWindow() { ShowWindow(hwnd_, SW_SHOW); ShowWindow(hwnd_, SW_RESTORE); SetForegroundWindow(hwnd_); }

    void ShowTrayMenu() {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, L"Open Mini Display");
        AppendMenuW(menu, MF_STRING | (playbackRunning_ ? MF_GRAYED : 0), 2, L"Start display");
        AppendMenuW(menu, MF_STRING | (!playbackRunning_ ? MF_GRAYED : 0), 3, L"Stop playback");
        AppendMenuW(menu, MF_STRING | (!display_ ? MF_GRAYED : 0), 4, L"Disconnect");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 5, L"Exit");
        POINT point{}; GetCursorPos(&point); SetForegroundWindow(hwnd_);
        const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd_, nullptr);
        DestroyMenu(menu);
        switch (command) {
        case 1: ShowMainWindow(); break;
        case 2: StartPlayback(); break;
        case 3: StopPlayback(); break;
        case 4: Disconnect(); break;
        case 5: ExitApplication(); break;
        }
    }

    void ExitApplication() {
        if (allowExit_) return;
        allowExit_ = true;
        StopPlayback(); Disconnect(); RemoveTrayIcon();
        DestroyWindow(hwnd_);
    }

    HINSTANCE instance_{};
    HWND hwnd_{};
    HFONT font_{};
    HWND deviceCombo_{}, refreshButton_{}, connectButton_{}, disconnectButton_{};
    HWND browseButton_{}, startButton_{}, stopButton_{}, mediaLabel_{};
    HWND scaleCombo_{}, rotationCombo_{}, fpsTrack_{}, fpsLabel_{}, startupCheck_{}, status_{}, preview_{};
    NOTIFYICONDATAW tray_{};
    ULONG_PTR gdiplusToken_{};
    std::vector<HidDeviceInfo> devices_;
    std::unique_ptr<HidDisplay> display_;
    std::unique_ptr<MediaAsset> media_;
    std::wstring lastMediaPath_;
    std::atomic<bool> playbackRunning_{false};
    std::atomic<bool> allowExit_{false};
    std::thread playbackThread_;
    HANDLE stopEvent_{};
    std::mutex previewMutex_;
    std::vector<BYTE> previewPixels_;
};

bool IsAutostart() {
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    bool result = false;
    if (args) {
        for (int i = 1; i < count; ++i) if (_wcsicmp(args[i], L"--autostart") == 0) result = true;
        LocalFree(args);
    }
    return result;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\ThermalrightMiniDisplayNativeV3");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        return 0;
    }
    App app(instance);
    const int result = app.Run(IsAutostart());
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return result;
}

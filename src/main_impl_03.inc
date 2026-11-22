            CW_USEDEFAULT, CW_USEDEFAULT, 820, 570, nullptr, nullptr, instance_, this);
        if (!hwnd_) return 4;
        AddTrayIcon();

        if (autostart) {
            ShowWindow(hwnd_, SW_HIDE);
            PostMessageW(hwnd_, WM_APP_AUTOSTART, 0, 0);
        } else {
            ShowWindow(hwnd_, SW_SHOW);
            UpdateWindow(hwnd_);
            RefreshDevices();
        }

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        App* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<App*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }
        return self ? self->HandleMessage(hwnd, message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE: CreateControls(); RestoreSettings(); return 0;
        case WM_COMMAND: OnCommand(LOWORD(wParam), HIWORD(wParam)); return 0;
        case WM_HSCROLL: UpdateFpsLabel(); return 0;
        case WM_CLOSE:
            if (!allowExit_) { ShowWindow(hwnd_, SW_HIDE); SetStatus(L"Running in the system tray. Display playback continues."); return 0; }
            DestroyWindow(hwnd_); return 0;
        case WM_DESTROY:
            RemoveTrayIcon(); PostQuitMessage(0); return 0;
        case WM_APP_TRAY:
            if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP) ShowMainWindow();
            else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) ShowTrayMenu();
            return 0;
        case WM_APP_STATUS: {
            std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
            if (text) SetStatus(*text);
            return 0;
        }
        case WM_APP_PREVIEW: InvalidateRect(preview_, nullptr, TRUE); return 0;
        case WM_APP_PLAYBACK_STOPPED:
            if (playbackThread_.joinable()) playbackThread_.join();
            EnableWindow(startButton_, display_ && media_);
            EnableWindow(stopButton_, FALSE);
            return 0;
        case WM_APP_AUTOSTART: AutoResume(); return 0;
        case WM_DRAWITEM: {
            auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (draw && draw->CtlID == IDC_PREVIEW) { PaintPreview(draw); return TRUE; }
            break;
        }
        default: return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }

    HWND MakeControl(const wchar_t* cls, const wchar_t* text, DWORD style, int id, int x, int y, int w, int h, DWORD ex = 0) {
        HWND control = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style,
            x, y, w, h, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        if (control && font_) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        return control;
    }

    void CreateControls() {
        font_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        MakeControl(L"STATIC", L"Detected BA120 Vision device", 0, 0, 24, 18, 300, 22);
        deviceCombo_ = MakeControl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IDC_DEVICE, 24, 43, 430, 180, WS_EX_CLIENTEDGE);
        refreshButton_ = MakeControl(L"BUTTON", L"Refresh", BS_PUSHBUTTON | WS_TABSTOP, IDC_REFRESH, 468, 43, 85, 28);
        connectButton_ = MakeControl(L"BUTTON", L"Connect", BS_PUSHBUTTON | WS_TABSTOP, IDC_CONNECT, 562, 43, 85, 28);
        disconnectButton_ = MakeControl(L"BUTTON", L"Disconnect", BS_PUSHBUTTON | WS_TABSTOP, IDC_DISCONNECT, 656, 43, 110, 28);
        EnableWindow(disconnectButton_, FALSE);

        MakeControl(L"STATIC", L"Media file", 0, 0, 24, 95, 100, 20);
        browseButton_ = MakeControl(L"BUTTON", L"Choose GIF / Image…", BS_PUSHBUTTON | WS_TABSTOP, IDC_BROWSE, 24, 119, 170, 30);
        mediaLabel_ = MakeControl(L"STATIC", L"No media selected", SS_LEFT, IDC_MEDIA, 208, 122, 230, 48);
        startButton_ = MakeControl(L"BUTTON", L"Start display", BS_PUSHBUTTON | WS_TABSTOP, IDC_START, 24, 176, 125, 30);
        stopButton_ = MakeControl(L"BUTTON", L"Stop", BS_PUSHBUTTON | WS_TABSTOP, IDC_STOP, 160, 176, 85, 30);
        EnableWindow(startButton_, FALSE);
        EnableWindow(stopButton_, FALSE);

        MakeControl(L"STATIC", L"Image scaling", 0, 0, 24, 230, 120, 20);
        scaleCombo_ = MakeControl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, IDC_SCALE, 24, 254, 180, 130);
        SendMessageW(scaleCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Fit / letterbox"));
        SendMessageW(scaleCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Fill / crop"));
        SendMessageW(scaleCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Stretch"));
        SendMessageW(scaleCombo_, CB_SETCURSEL, 0, 0);

        MakeControl(L"STATIC", L"Rotation", 0, 0, 228, 230, 100, 20);
        rotationCombo_ = MakeControl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, IDC_ROTATION, 228, 254, 110, 100);
        SendMessageW(rotationCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"90°"));
        SendMessageW(rotationCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"270°"));
        SendMessageW(rotationCombo_, CB_SETCURSEL, 0, 0);

        MakeControl(L"STATIC", L"Maximum GIF frame rate", 0, 0, 24, 310, 200, 20);
        fpsTrack_ = MakeControl(TRACKBAR_CLASSW, L"", TBS_AUTOTICKS | TBS_HORZ | WS_TABSTOP, IDC_FPS, 24, 335, 300, 38);
        SendMessageW(fpsTrack_, TBM_SETRANGE, TRUE, MAKELONG(1, 12));
        SendMessageW(fpsTrack_, TBM_SETPOS, TRUE, 8);
        fpsLabel_ = MakeControl(L"STATIC", L"8 FPS", SS_LEFT, IDC_FPS_LABEL, 335, 342, 70, 22);

        startupCheck_ = MakeControl(L"BUTTON", L"Start with Windows (minimised to tray)", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_STARTUP, 24, 395, 310, 28);

        MakeControl(L"STATIC", L"LCD preview · 320 × 240 logical", 0, 0, 470, 95, 260, 20);
        preview_ = MakeControl(L"STATIC", L"", SS_OWNERDRAW, IDC_PREVIEW, 470, 120, 300, 225, WS_EX_CLIENTEDGE);
        MakeControl(L"STATIC", L"BA120 Vision · 240 × 320 · RGB565 · HID Type-2", SS_LEFT, 0, 440, 370, 340, 28);
        status_ = MakeControl(L"STATIC", L"Ready. Refresh to scan for the cooler.", SS_LEFT, IDC_STATUS, 24, 468, 745, 42, WS_EX_CLIENTEDGE);

        SendMessageW(preview_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        SendMessageW(startupCheck_, BM_SETCHECK, BST_UNCHECKED, 0);
    }

    void RestoreSettings() {
        const auto scale = ReadSetting(L"ScaleMode", L"0");
        const auto rotation = ReadSetting(L"Rotation", L"90");
        const auto fps = ReadSetting(L"MaximumFps", L"8");
        const auto startup = ReadSetting(L"StartWithWindows", L"0");
        lastMediaPath_ = ReadSetting(L"LastMediaPath", L"");
        SendMessageW(scaleCombo_, CB_SETCURSEL, std::clamp(_wtoi(scale.c_str()), 0, 2), 0);
        SendMessageW(rotationCombo_, CB_SETCURSEL, _wtoi(rotation.c_str()) == 270 ? 1 : 0, 0);
        SendMessageW(fpsTrack_, TBM_SETPOS, TRUE, std::clamp(_wtoi(fps.c_str()), 1, 12));
        SendMessageW(startupCheck_, BM_SETCHECK, _wtoi(startup.c_str()) ? BST_CHECKED : BST_UNCHECKED, 0);
        UpdateFpsLabel();
    }

    void SaveSettings() {
        wchar_t buffer[32]{};
        wsprintfW(buffer, L"%d", static_cast<int>(SendMessageW(scaleCombo_, CB_GETCURSEL, 0, 0)));
        WriteSetting(L"ScaleMode", buffer);
        wsprintfW(buffer, L"%d", SendMessageW(rotationCombo_, CB_GETCURSEL, 0, 0) == 1 ? 270 : 90);
        WriteSetting(L"Rotation", buffer);
        wsprintfW(buffer, L"%d", static_cast<int>(SendMessageW(fpsTrack_, TBM_GETPOS, 0, 0)));
        WriteSetting(L"MaximumFps", buffer);
        WriteSetting(L"LastMediaPath", lastMediaPath_);
        const bool startup = SendMessageW(startupCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        WriteSetting(L"StartWithWindows", startup ? L"1" : L"0");
    }

    void ApplyStartup(bool enabled) {
        HKEY key{};
        LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (result != ERROR_SUCCESS) throw std::runtime_error("Cannot open the current-user Run registry key.");
        if (enabled) {
            wchar_t exe[MAX_PATH]{};
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            std::wstring command = L"\"" + std::wstring(exe) + L"\" --autostart";
            result = RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        } else {
            result = RegDeleteValueW(key, kRunValue);
            if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
        }
        RegCloseKey(key);
        if (result != ERROR_SUCCESS) throw std::runtime_error("Could not update the Windows startup entry.");
    }

    void OnCommand(int id, int notification) {
        if (id == IDC_REFRESH && notification == BN_CLICKED) RefreshDevices();
        else if (id == IDC_CONNECT && notification == BN_CLICKED) ConnectDevice();
        else if (id == IDC_DISCONNECT && notification == BN_CLICKED) Disconnect();
        else if (id == IDC_BROWSE && notification == BN_CLICKED) BrowseMedia();
        else if (id == IDC_START && notification == BN_CLICKED) StartPlayback();
        else if (id == IDC_STOP && notification == BN_CLICKED) StopPlayback();
        else if (id == IDC_STARTUP && notification == BN_CLICKED) {
            const bool enabled = SendMessageW(startupCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            try { ApplyStartup(enabled); SaveSettings(); SetStatus(enabled ? L"Windows startup enabled." : L"Windows startup disabled."); }
            catch (const std::exception& ex) { SetStatus(L"Startup setting failed: " + Utf8ToWide(ex.what())); }
        }
        else if ((id == IDC_SCALE || id == IDC_ROTATION) && notification == CBN_SELCHANGE) {
            SaveSettings(); RefreshPreview();
        }
    }

    void UpdateFpsLabel() {
        const int fps = static_cast<int>(SendMessageW(fpsTrack_, TBM_GETPOS, 0, 0));
        wchar_t label[24]{}; wsprintfW(label, L"%d FPS", fps);
        SetWindowTextW(fpsLabel_, label);
        SaveSettings();
    }

    void SetStatus(const std::wstring& text) { if (status_) SetWindowTextW(status_, text.c_str()); }
    void PostStatus(const std::wstring& text) {
        if (!hwnd_) return;
        auto* message = new std::wstring(text);
        if (!PostMessageW(hwnd_, WM_APP_STATUS, 0, reinterpret_cast<LPARAM>(message))) delete message;
    }

    void RefreshDevices() {
        SetStatus(L"Scanning Windows HID interfaces…");
        SendMessageW(deviceCombo_, CB_RESETCONTENT, 0, 0);
        devices_.clear();
        try {
            devices_ = EnumerateBa120();
            for (size_t i = 0; i < devices_.size(); ++i) {
                SendMessageW(deviceCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(devices_[i].label.c_str()));
            }
            if (!devices_.empty()) {
                SendMessageW(deviceCombo_, CB_SETCURSEL, 0, 0);
                SetStatus(L"Found " + std::to_wstring(devices_.size()) + L" BA120-compatible HID interface(s).");
            } else SetStatus(L"No BA120 HID interface found. Close LOU Control Center / TRCC if either is using the display.");
        } catch (const std::exception& ex) { SetStatus(L"Device scan failed: " + Utf8ToWide(ex.what())); }
    }

    void ConnectDevice() {
        if (display_) return;
        const LRESULT selected = SendMessageW(deviceCombo_, CB_GETCURSEL, 0, 0);
        if (selected == CB_ERR || static_cast<size_t>(selected) >= devices_.size()) { SetStatus(L"Refresh and select the BA120 device first."); return; }
        std::wstring conflict;
        if (HasControllerConflict(conflict)) { SetStatus(L"Close " + conflict + L" before connecting; only one program can control the LCD."); return; }
        try {
            auto display = std::make_unique<HidDisplay>(devices_[static_cast<size_t>(selected)]);
            display->Connect();
            display_ = std::move(display);
            EnableWindow(connectButton_, FALSE); EnableWindow(disconnectButton_, TRUE);
            EnableWindow(deviceCombo_, FALSE); EnableWindow(startButton_, media_ != nullptr);
            SetStatus(L"Connected · BA120 Vision · PM52 / SUB0 · 240 × 320 RGB565.");
        } catch (const std::exception& ex) {
            display_.reset(); SetStatus(L"Connection failed: " + Utf8ToWide(ex.what()));
        }
    }

    void Disconnect() {
        StopPlayback();
        display_.reset();
        EnableWindow(connectButton_, TRUE); EnableWindow(disconnectButton_, FALSE);
        EnableWindow(deviceCombo_, TRUE); EnableWindow(startButton_, FALSE);
        SetStatus(L"Disconnected. The panel may return to its factory screen after its keep-alive timeout.");
    }

    void BrowseMedia() {
        wchar_t path[MAX_PATH]{};
        if (!lastMediaPath_.empty()) wcsncpy_s(path, lastMediaPath_.c_str(), _TRUNCATE);
        const wchar_t filter[] = L"Images and GIFs\0*.gif;*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0GIF animation\0*.gif\0Image files\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0All files\0*.*\0\0";
        OPENFILENAMEW dialog{};

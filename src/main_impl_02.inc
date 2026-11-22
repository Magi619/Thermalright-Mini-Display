    void SendFrame(const std::vector<std::array<BYTE, kReportBytes>>& reports, const HANDLE stopEvent) {
        if (!IsConnected()) throw std::runtime_error("The BA120 is not connected.");
        if (reports.size() != kFrameReports) throw std::runtime_error("Invalid BA120 frame packet count.");
        for (const auto& report : reports) {
            if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) return;
            WriteReport(report.data(), static_cast<DWORD>(report.size()), 2500);
        }
    }

    void Close() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CancelIoEx(handle_, nullptr);
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
        if (writeEvent_) { CloseHandle(writeEvent_); writeEvent_ = nullptr; }
        if (readEvent_) { CloseHandle(readEvent_); readEvent_ = nullptr; }
    }

private:
    bool WriteReport(const BYTE* data, DWORD length, DWORD timeout) {
        ResetEvent(writeEvent_);
        OVERLAPPED overlapped{};
        overlapped.hEvent = writeEvent_;
        DWORD written = 0;
        BOOL ok = WriteFile(handle_, data, length, &written, &overlapped);
        if (!ok && GetLastError() != ERROR_IO_PENDING) throw std::runtime_error("HID write failed: " + std::to_string(GetLastError()));
        if (!ok) {
            DWORD wait = WaitForSingleObject(writeEvent_, timeout);
            if (wait != WAIT_OBJECT_0) {
                CancelIoEx(handle_, &overlapped);
                WaitForSingleObject(writeEvent_, 250);
                throw std::runtime_error("HID write timed out.");
            }
        }
        if (!GetOverlappedResult(handle_, &overlapped, &written, FALSE))
            throw std::runtime_error("HID write completion failed: " + std::to_string(GetLastError()));
        if (written != length) throw std::runtime_error("HID accepted an incomplete output report.");
        return true;
    }

    bool ReadReport(BYTE* data, DWORD length, DWORD timeout, DWORD& received) {
        ResetEvent(readEvent_);
        OVERLAPPED overlapped{};
        overlapped.hEvent = readEvent_;
        received = 0;
        BOOL ok = ReadFile(handle_, data, length, &received, &overlapped);
        if (!ok && GetLastError() != ERROR_IO_PENDING) throw std::runtime_error("HID read failed: " + std::to_string(GetLastError()));
        if (!ok) {
            DWORD wait = WaitForSingleObject(readEvent_, timeout);
            if (wait != WAIT_OBJECT_0) {
                CancelIoEx(handle_, &overlapped);
                WaitForSingleObject(readEvent_, 250);
                throw std::runtime_error("HID identity request timed out.");
            }
        }
        if (!GetOverlappedResult(handle_, &overlapped, &received, FALSE))
            throw std::runtime_error("HID response completion failed: " + std::to_string(GetLastError()));
        return received > 0;
    }

    HidDeviceInfo info_;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    HANDLE writeEvent_ = nullptr;
    HANDLE readEvent_ = nullptr;
};

struct MediaAsset {
    std::wstring path;
    std::unique_ptr<Image> image;
    GUID frameDimension{};
    UINT frameCount = 1;
    bool animated = false;
    std::vector<UINT> delaysMs;

    static std::unique_ptr<MediaAsset> Load(const std::wstring& path) {
        auto media = std::make_unique<MediaAsset>();
        media->path = path;
        media->image.reset(Image::FromFile(path.c_str(), FALSE));
        if (!media->image || media->image->GetLastStatus() != Ok)
            throw std::runtime_error("GDI+ could not decode this image or GIF.");

        GUID rawFormat{};
        if (media->image->GetRawFormat(&rawFormat) == Ok && IsEqualGUID(rawFormat, ImageFormatGIF)) {
            UINT count = media->image->GetFrameDimensionsCount();
            if (count) {
                std::vector<GUID> dimensions(count);
                if (media->image->GetFrameDimensionsList(dimensions.data(), count) == Ok) {
                    for (const GUID& dimension : dimensions) {
                        if (IsEqualGUID(dimension, FrameDimensionTime)) {
                            media->frameDimension = dimension;
                            media->frameCount = media->image->GetFrameCount(&media->frameDimension);
                            media->animated = media->frameCount > 1;
                            break;
                        }
                    }
                }
            }
        }
        if (media->animated) {
            media->delaysMs.assign(media->frameCount, 100);
            UINT propertySize = media->image->GetPropertyItemSize(PropertyTagFrameDelay);
            if (propertySize) {
                std::vector<BYTE> buffer(propertySize);
                auto* property = reinterpret_cast<PropertyItem*>(buffer.data());
                if (media->image->GetPropertyItem(PropertyTagFrameDelay, propertySize, property) == Ok &&
                    property->type == PropertyTagTypeLong && property->value) {
                    const auto* delays = reinterpret_cast<const ULONG*>(property->value);
                    const UINT entries = std::min<UINT>(media->frameCount, property->length / sizeof(ULONG));
                    for (UINT i = 0; i < entries; ++i) {
                        UINT value = delays[i] * 10;
                        media->delaysMs[i] = value == 0 ? 100 : std::clamp<UINT>(value, 20, 10000);
                    }
                }
            }
        }
        return media;
    }

    UINT Delay(UINT frame) const {
        if (!animated || frame >= delaysMs.size()) return 2000;
        return delaysMs[frame];
    }
};

struct EncodedFrame {
    std::vector<std::array<BYTE, kReportBytes>> reports;
    std::vector<BYTE> previewBgra;
    UINT delayMs{};
};

EncodedFrame EncodeFrame(MediaAsset& media, UINT frameIndex, int scaleMode, int rotation) {
    if (media.animated) {
        if (media.image->SelectActiveFrame(&media.frameDimension, frameIndex) != Ok)
            throw std::runtime_error("Could not select GIF animation frame.");
    }

    const UINT sourceWidth = media.image->GetWidth();
    const UINT sourceHeight = media.image->GetHeight();
    if (!sourceWidth || !sourceHeight) throw std::runtime_error("The image has invalid dimensions.");

    Bitmap logical(kLogicalWidth, kLogicalHeight, PixelFormat32bppARGB);
    if (logical.GetLastStatus() != Ok) throw std::runtime_error("Could not allocate the 320 x 240 frame buffer.");
    Graphics graphics(&logical);
    graphics.SetCompositingMode(CompositingModeSourceOver);
    graphics.SetCompositingQuality(CompositingQualityHighQuality);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
    graphics.Clear(Color(255, 0, 0, 0));

    RectF destination(0, 0, static_cast<REAL>(kLogicalWidth), static_cast<REAL>(kLogicalHeight));
    if (scaleMode != 2) {
        const double sx = static_cast<double>(kLogicalWidth) / sourceWidth;
        const double sy = static_cast<double>(kLogicalHeight) / sourceHeight;
        const double scale = scaleMode == 1 ? std::max(sx, sy) : std::min(sx, sy);
        const REAL width = static_cast<REAL>(sourceWidth * scale);
        const REAL height = static_cast<REAL>(sourceHeight * scale);
        destination = RectF((kLogicalWidth - width) / 2.0f, (kLogicalHeight - height) / 2.0f, width, height);
    }
    if (graphics.DrawImage(media.image.get(), destination, 0, 0, sourceWidth, sourceHeight, UnitPixel) != Ok)
        throw std::runtime_error("Could not render the selected image frame.");

    BitmapData locked{};
    Rect logicalRect(0, 0, kLogicalWidth, kLogicalHeight);
    if (logical.LockBits(&logicalRect, ImageLockModeRead, PixelFormat32bppARGB, &locked) != Ok)
        throw std::runtime_error("Could not read the rendered image pixels.");

    EncodedFrame result;
    result.previewBgra.resize(kLogicalWidth * kLogicalHeight * 4);
    for (int y = 0; y < kLogicalHeight; ++y) {
        const BYTE* row = static_cast<const BYTE*>(locked.Scan0) + y * locked.Stride;
        std::memcpy(result.previewBgra.data() + y * kLogicalWidth * 4, row, kLogicalWidth * 4);
    }
    logical.UnlockBits(&locked);

    std::vector<BYTE> rgb565(kPanelWidth * kPanelHeight * 2);
    for (int y = 0; y < kLogicalHeight; ++y) {
        for (int x = 0; x < kLogicalWidth; ++x) {
            const size_t src = (static_cast<size_t>(y) * kLogicalWidth + x) * 4;
            int outX = 0, outY = 0;
            if (rotation == 270) { outX = y; outY = kLogicalWidth - 1 - x; }
            else { outX = kLogicalHeight - 1 - y; outY = x; } // 90 degrees clockwise
            const BYTE blue = result.previewBgra[src + 0];
            const BYTE green = result.previewBgra[src + 1];
            const BYTE red = result.previewBgra[src + 2];
            const uint16_t value = static_cast<uint16_t>(((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3));
            const size_t target = (static_cast<size_t>(outY) * kPanelWidth + outX) * 2;
            rgb565[target] = static_cast<BYTE>(value & 0xff);
            rgb565[target + 1] = static_cast<BYTE>(value >> 8);
        }
    }

    result.reports.resize(kFrameReports);
    for (auto& report : result.reports) report.fill(0);
    // Protocol packet 0: 20-byte header followed by the first 492 RGB565 bytes.
    auto& first = result.reports[0];
    std::copy(std::begin(kMagic), std::end(kMagic), first.begin() + 1);
    first[5] = 0x02; // Type-2 frame command
    first[7] = 0x01; // RGB565 pixel mode
    first[9] = static_cast<BYTE>(kPanelWidth & 0xff);
    first[10] = static_cast<BYTE>((kPanelWidth >> 8) & 0xff);
    first[11] = static_cast<BYTE>(kPanelHeight & 0xff);
    first[12] = static_cast<BYTE>((kPanelHeight >> 8) & 0xff);
    first[13] = 0x02;
    const uint32_t payloadLength = static_cast<uint32_t>(rgb565.size());
    for (int b = 0; b < 4; ++b) first[17 + b] = static_cast<BYTE>((payloadLength >> (8 * b)) & 0xff);
    std::memcpy(first.data() + 21, rgb565.data(), 492);

    size_t sourceOffset = 492;
    for (int packet = 1; packet < kFrameReports; ++packet) {
        auto& report = result.reports[packet];
        const size_t count = std::min<size_t>(512, rgb565.size() - sourceOffset);
        std::memcpy(report.data() + 1, rgb565.data() + sourceOffset, count);
        sourceOffset += count;
    }
    result.delayMs = media.Delay(frameIndex);
    return result;
}

class App {
public:
    explicit App(HINSTANCE instance) : instance_(instance) {}
    ~App() { StopPlayback(); Disconnect(); if (gdiplusToken_) GdiplusShutdown(gdiplusToken_); }

    int Run(bool autostart) {
        GdiplusStartupInput gdiplusInput;
        if (GdiplusStartup(&gdiplusToken_, &gdiplusInput, nullptr) != Ok) return 2;
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&controls);

        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        // Use the Windows Shell32 monitor-and-keyboard system icon embedded at build time.
        wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
        wc.hIconSm = wc.hIcon;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"ThermalrightMiniDisplayNativeV3";
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 3;

        hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"Thermalright Mini Display · Native C++ v3.0.8",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,

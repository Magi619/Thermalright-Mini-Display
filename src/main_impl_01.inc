#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <gdiplus.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <winreg.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <iterator>
#include <cwctype>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "Gdiplus.lib")
#pragma comment(lib, "Hid.lib")
#pragma comment(lib, "Setupapi.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Advapi32.lib")

using namespace Gdiplus;

namespace {
constexpr wchar_t kAppName[] = L"Thermalright Mini Display";
constexpr wchar_t kSettingsFolder[] = L"ThermalrightMiniDisplayNative";
constexpr wchar_t kSettingsFile[] = L"settings.ini";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"ThermalrightMiniDisplay";
constexpr USHORT kVendorId = 0x0416;
constexpr USHORT kProductId = 0x5302;
constexpr int kLogicalWidth = 320;
constexpr int kLogicalHeight = 240;
constexpr int kPanelWidth = 240;
constexpr int kPanelHeight = 320;
constexpr int kReportBytes = 513; // Windows HID report ID + 512 protocol bytes
constexpr int kFrameReports = 301;
constexpr DWORD kKeepAliveIntervalMs = 2000; // BA120 reverts to its factory screen after ~3 s without HID refresh
constexpr UINT WM_APP_TRAY = WM_APP + 10;
constexpr UINT WM_APP_STATUS = WM_APP + 11;
constexpr UINT WM_APP_PREVIEW = WM_APP + 12;
constexpr UINT WM_APP_PLAYBACK_STOPPED = WM_APP + 13;
constexpr UINT WM_APP_AUTOSTART = WM_APP + 14;
constexpr UINT_PTR kTrayId = 1;
constexpr WORD IDI_APP_ICON = 101; // Embedded Windows Shell32 monitor-and-keyboard icon

constexpr int IDC_DEVICE = 1001;
constexpr int IDC_REFRESH = 1002;
constexpr int IDC_CONNECT = 1003;
constexpr int IDC_DISCONNECT = 1004;
constexpr int IDC_BROWSE = 1005;
constexpr int IDC_START = 1006;
constexpr int IDC_STOP = 1007;
constexpr int IDC_SCALE = 1008;
constexpr int IDC_ROTATION = 1009;
constexpr int IDC_FPS = 1010;
constexpr int IDC_STARTUP = 1011;
constexpr int IDC_STATUS = 1012;
constexpr int IDC_MEDIA = 1013;
constexpr int IDC_PREVIEW = 1014;
constexpr int IDC_FPS_LABEL = 1015;

const BYTE kMagic[4] = {0xDA, 0xDB, 0xDC, 0xDD};

std::wstring GetLastErrorText(DWORD error = GetLastError()) {
    wchar_t* buffer = nullptr;
    DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring result = length && buffer ? std::wstring(buffer, length) : L"Windows error " + std::to_wstring(error);
    if (buffer) LocalFree(buffer);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' ')) result.pop_back();
    return result;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (count <= 1) return std::wstring(value.begin(), value.end());
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), count);
    result.pop_back();
    return result;
}

std::wstring GetSettingsDirectory() {
    wchar_t base[MAX_PATH]{};
    DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (!size || size >= MAX_PATH) return L".";
    return std::wstring(base) + L"\\" + kSettingsFolder;
}

std::wstring GetSettingsPath() { return GetSettingsDirectory() + L"\\" + kSettingsFile; }

void EnsureSettingsDirectory() {
    const auto dir = GetSettingsDirectory();
    CreateDirectoryW(dir.c_str(), nullptr);
}

std::wstring ReadSetting(const wchar_t* key, const wchar_t* fallback) {
    wchar_t value[4096]{};
    GetPrivateProfileStringW(L"Display", key, fallback, value, static_cast<DWORD>(std::size(value)), GetSettingsPath().c_str());
    return value;
}

void WriteSetting(const wchar_t* key, const std::wstring& value) {
    EnsureSettingsDirectory();
    WritePrivateProfileStringW(L"Display", key, value.c_str(), GetSettingsPath().c_str());
}

bool IsProcessRunning(const wchar_t* wanted) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, wanted) == 0) { found = true; break; }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

bool HasControllerConflict(std::wstring& name) {
    const wchar_t* conflicts[] = {L"Lou.ControlCenter.exe", L"TRCC.exe", L"USBLCDNEW.exe", L"USBLCD.exe"};
    for (auto* item : conflicts) {
        if (IsProcessRunning(item)) { name = item; return true; }
    }
    return false;
}

struct HidDeviceInfo {
    std::wstring path;
    USHORT inputLength{};
    USHORT outputLength{};
    std::wstring label;
};

std::vector<HidDeviceInfo> EnumerateBa120() {
    GUID hidGuid{};
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO info = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (info == INVALID_HANDLE_VALUE) throw std::runtime_error("SetupDiGetClassDevs failed: " + std::to_string(GetLastError()));

    std::vector<HidDeviceInfo> found;
    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA iface{};
        iface.cbSize = sizeof(iface);
        if (!SetupDiEnumDeviceInterfaces(info, nullptr, &hidGuid, index, &iface)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
            continue;
        }
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(info, &iface, nullptr, 0, &required, nullptr);
        if (!required) continue;
        std::vector<BYTE> storage(required);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(info, &iface, detail, required, nullptr, nullptr)) continue;

        std::wstring path = detail->DevicePath;
        std::wstring upper = path;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
        if (upper.find(L"&MI_") != std::wstring::npos && upper.find(L"&MI_00") == std::wstring::npos) continue;

        HANDLE device = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (device == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        bool matching = HidD_GetAttributes(device, &attributes) && attributes.VendorID == kVendorId && attributes.ProductID == kProductId;
        CloseHandle(device);
        if (!matching) continue;

        // Windows ReadFile accepts a buffer at least as large as the HID input
        // report. Use the protocol's established 513-byte maximum buffer so
        // this app does not need HidP_GetCaps / hidparse.lib (which is not included
        // with every desktop SDK workload). The
        // actual byte count is returned by ReadFile and validated by handshake.
        HidDeviceInfo item;
        item.path = std::move(path);
        item.inputLength = kReportBytes;
        item.outputLength = kReportBytes;
        item.label = L"Thermalright BA120 Vision  |  HID interface 0  |  0416:5302";
        if (std::none_of(found.begin(), found.end(), [&](const auto& x) { return _wcsicmp(x.path.c_str(), item.path.c_str()) == 0; }))
            found.push_back(std::move(item));
    }
    SetupDiDestroyDeviceInfoList(info);
    return found;
}

class HidDisplay {
public:
    explicit HidDisplay(const HidDeviceInfo& info) : info_(info) {}
    ~HidDisplay() { Close(); }
    HidDisplay(const HidDisplay&) = delete;
    HidDisplay& operator=(const HidDisplay&) = delete;

    void Connect() {
        if (handle_ != INVALID_HANDLE_VALUE) return;
        handle_ = CreateFileW(info_.path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open BA120 HID interface: " + std::to_string(GetLastError()));
        writeEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        readEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!writeEvent_ || !readEvent_) { Close(); throw std::runtime_error("Cannot create HID I/O events."); }
        HidD_SetNumInputBuffers(handle_, 64);

        std::array<BYTE, kReportBytes> handshake{};
        std::copy(std::begin(kMagic), std::end(kMagic), handshake.begin() + 1);
        handshake[13] = 0x01;
        WriteReport(handshake.data(), static_cast<DWORD>(handshake.size()), 2500);

        std::vector<BYTE> response(info_.inputLength);
        DWORD received = 0;
        if (!ReadReport(response.data(), static_cast<DWORD>(response.size()), 2500, received) || received < 5)
            throw std::runtime_error("BA120 did not answer its HID identity request.");
        size_t offset = (received >= 5 && response[0] == 0 && std::memcmp(response.data() + 1, kMagic, 4) == 0) ? 1 : 0;
        if (received < offset + 13 || std::memcmp(response.data() + offset, kMagic, 4) != 0)
            throw std::runtime_error("Unexpected Thermalright HID response signature.");
        const BYTE sub = response[offset + 4];
        const BYTE pm = response[offset + 5];
        if (response[offset + 12] != 0x01) throw std::runtime_error("Thermalright identity reply has an invalid ready marker.");
        if (pm != 52 || sub != 0) throw std::runtime_error("Detected a panel other than BA120 Vision PM52 / SUB0.");
    }

    bool IsConnected() const { return handle_ != INVALID_HANDLE_VALUE; }

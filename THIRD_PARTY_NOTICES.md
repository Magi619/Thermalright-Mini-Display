# Third-party components

This project does not bundle third-party libraries. It uses APIs provided by Windows:

- Win32 API and common controls
- Windows HID API (\`Hid.dll\`)
- SetupAPI
- GDI+ (\`Gdiplus.dll\`)
- Shell notification-area API
- Windows Registry API
- Windows Shell system icon resources

These components are supplied by Windows and the Windows SDK. The project source does not redistribute Microsoft system DLLs or SDK binaries. Windows and Visual Studio remain subject to their respective Microsoft license terms.

# Thermalright Mini Display

A lightweight native Windows companion for the Thermalright Burst Assassin 120 Vision (BA120 Vision) USB LCD. The application is designed to display user-selected still images and animated GIFs on the cooler's integrated 2.4-inch panel, without requiring a hardware-monitoring dashboard or a heavyweight control centre.

## Project overview

Thermalright Mini Display was created to provide a focused alternative to larger cooler-control applications. Its purpose is simple: let the owner choose an image or animation, prepare it for the LCD, and transfer the resulting frames to the cooler over USB HID. The application deliberately avoids unrelated monitoring widgets, overlays, fan curves and system-statistics panels so that the background process can remain small and easy to understand.

The project evolved through several prototypes. Earlier experiments used Python and later a managed Windows desktop implementation. Those versions helped establish the display's USB identity, frame layout and refresh behaviour. The current v3 line is a native C++/Win32 implementation intended to reduce runtime overhead and avoid bundling a managed desktop runtime. The v3.0.8 source is the current project baseline represented by this repository snapshot.

## Hardware and display profile

The confirmed target hardware in this project is the Thermalright Burst Assassin 120 Vision White cooler. The display profile used during development identifies a USB HID device with Vendor ID \`0416\`, Product ID \`5302\`, panel mode \`0x34\` (decimal 52), and sub-mode \`0\`. The panel's physical raster is treated as 240 × 320 pixels, while the application's logical image canvas is 320 × 240 pixels before the configured orientation transform. Pixel data is converted to RGB565 little-endian for transport.

The supported USB transport profile uses HID Type-2 output reports. The known report buffer is 513 bytes including the leading report-ID byte, and a complete still frame is transmitted as a sequence of approximately 301 report packets. These values are hardware-profile details observed during development; they should not be assumed to apply to other Thermalright displays or revisions without verification. The detailed packet layout and handshake fields are documented in [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Features

- Load a still image and display it on the cooler's LCD.
- Play animated GIF files, with frame selection and timing handled by the application.
- Decode common raster images and GIFs using Windows GDI+.
- Prepare images for the 320 × 240 logical canvas and convert pixels to RGB565 little-endian.
- Communicate with the supported display over the Windows HID and SetupAPI interfaces.
- Keep a selected static image present using a periodic keep-alive frame transfer. Development testing found that the BA120 can revert to its factory screen after several seconds without additional USB frame traffic.
- Use a system-tray workflow for background operation.
- Save the selected media path, image scaling, rotation, GIF frame-rate cap and Windows startup preference in a small settings file under the current user's Local AppData folder.
- Target a small native executable without a .NET runtime dependency.

## Performance goals and measured observations

Low idle CPU usage and low memory consumption are central design goals. In user testing of a native v3 build, Task Manager showed approximately 8.7 MB of memory usage. A static image reached 0.0% CPU in the observed Task Manager sample when no frame was being changed, while animated GIF playback showed approximately 0.4–0.6% CPU depending on playback. These are individual observations from one machine and one measurement window, not guaranteed benchmarks or formal test results.

A static image cannot simply be sent once and then left untouched indefinitely on the confirmed BA120 profile: the display reverted to its factory screen after around three seconds. The native app therefore needs a low-frequency keep-alive transfer for static content. The image is decoded and converted once, then the prepared packet sequence is reused for subsequent refreshes. Animated GIFs necessarily require recurring frame selection, rendering, pixel conversion and USB transfers; their CPU usage cannot be literally zero while animation is active.

## Build requirements

- Windows 10 or Windows 11, x64.
- Visual Studio 2022 or newer with the **Desktop development with C++** workload.
- MSVC x64/x86 C++ build tools (v143 or newer where available).
- A Windows 10/11 SDK.
- Windows PowerShell and the Windows Resource Compiler (\`rc.exe\`) for extracting and embedding the Windows Shell32 application icon.
- The BA120 display connected by USB for hardware testing.

The .NET SDK alone is not sufficient. It does not provide the native MSVC compiler. Open the Visual Studio x64 Native Tools command prompt when diagnosing a toolchain problem.

## Build

1. Clone or download this repository.
2. Open the project folder in File Explorer.
3. Run \`CHECK_CPP_TOOLCHAIN.bat\` if you want to verify MSVC, the linker, the resource compiler and the Windows SDK.
4. Run \`BUILD_CPP_APP.bat\`.
5. The script initialises the installed Visual Studio C++ environment, locates the Windows SDK, extracts the built-in Windows Shell32 monitor-and-keyboard icon, compiles the x64 executable and places the output under \`publish\`.
6. Run \`publish\\ThermalrightMiniDisplay.exe\`.

The native translation unit is assembled from ordered implementation fragments under \`src\`. The build script compiles \`src\\main.cpp\`, which includes those fragments in sequence. This keeps the source in ordinary editable text files while preserving one compiled translation unit.

If the build reports that \`cl.exe\` cannot be found, verify that the Desktop development with C++ workload and MSVC x64 tools are installed. If the linker reports a missing Windows SDK library, check the selected SDK's \`Lib\` folder and the project's linker dependencies rather than installing unrelated .NET components.

## Supported device profile and safety

The current project is focused on the BA120 Vision profile listed above. Do not assume it supports every Thermalright cooler, panel generation, USB identifier or firmware. The application identifies the expected VID/PID and validates the PM/SUB handshake before sending display data. Do not send arbitrary HID reports to unknown devices. Keep a known-working version available while testing protocol or playback changes.

## Repository layout

- \`src/main.cpp\` — translation-unit entry point.
- \`src/main_impl_*.inc\` — ordered native C++ implementation fragments.
- \`BUILD_CPP_APP.bat\` — x64 MSVC build and publish script.
- \`CHECK_CPP_TOOLCHAIN.bat\` — Visual Studio / MSVC / Windows SDK diagnostic script.
- \`scripts/Extract-WindowsMonitorIcon.ps1\` — extracts the built-in Windows Shell32 icon and saves it for resource compilation.
- \`docs/PROTOCOL.md\` — BA120 device identity, handshake and frame-packet notes.
- \`VERSION.txt\` — current source and build metadata.
- \`THIRD_PARTY_NOTICES.md\` — Windows system components used by the project.

Generated object files, intermediate build directories, debug symbols and published executables are excluded from version control through \`.gitignore\`. Build output should be recreated locally rather than committed as source.

## Troubleshooting

### The screen returns to the factory display

The BA120 profile tested during development reverted to its default display after approximately three seconds without refreshed image data. Static mode therefore sends a cached frame packet sequence periodically. If the timeout changes between firmware revisions, adjust the keep-alive interval conservatively and verify over an extended test.

### GIF shows black lines or incomplete frames

GIF frames may use partial rectangles, transparency and disposal operations. GDI+ frame selection and rendering are used before resizing and RGB565 conversion. Incorrect stride handling or animation-frame composition can produce horizontal artefacts. Test with more than one GIF, including one with transparency and one with partial-frame updates.

### Compiler or SDK not detected

Use **x64 Native Tools Command Prompt for Visual Studio** and run \`where cl\`, \`where link\`, \`where rc\`, \`echo %WindowsSdkDir%\` and \`echo %WindowsSDKVersion%\`. Confirm that the selected Windows SDK contains \`um\\Windows.h\` and the required x64 import libraries. The included \`CHECK_CPP_TOOLCHAIN.bat\` script can perform the same basic checks.

### CPU usage is not zero during GIF playback

Animated content requires frame selection, rendering, pixel conversion and USB transfers. Lowering the GIF frame rate can reduce activity, but a nonzero reading during active animation is expected. Static content can show 0.0% in Task Manager between periodic keep-alive writes, depending on measurement interval and scheduler sampling.

## Development status

This repository documents the v3.0.8 project baseline and the BA120 profile established during hands-on development. The source should be treated as a hardware-specific community project. Build success, display compatibility, image quality, GIF disposal behaviour, tray operation and performance should be verified on the target PC before treating a release as stable. No claim is made that every build in the version history has been independently compiled or physically validated.

## Contributing

Bug reports and improvements are welcome. When reporting a problem, include the application version, Windows version, compiler/SDK version, exact build output, display model, observed symptom, and whether the issue occurs with both a static image and a GIF. Do not attach private keys, access tokens or unrelated personal data. See [CONTRIBUTING.md](CONTRIBUTING.md) and [SECURITY.md](SECURITY.md) for repository guidance.

## Third-party components

The application relies on Windows system APIs and SDK components. Review [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for applicable notices. Windows system libraries are not redistributed as application source.

## Disclaimer

This is an independent community project and is not an official Thermalright application. Thermalright is mentioned only to identify the compatible hardware. Use at your own risk and do not use this software with an unsupported device or protocol profile.

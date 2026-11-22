# Contributing

Thank you for considering a contribution to Thermalright Mini Display. The project is a small, hardware-focused Windows utility, so changes should preserve the supported BA120 protocol profile and the application's low-overhead design.

## Before opening a change

1. Check whether the issue is reproducible with the latest source on the supported BA120 Vision hardware.
2. Search existing issues and discussions for the same behaviour.
3. Keep unrelated features out of protocol, image-rendering and HID changes.
4. Do not claim compatibility with another cooler or panel revision unless it has been tested with that hardware.

## Build and test

Build with Visual Studio 2022 or newer, the Desktop development with C++ workload, MSVC x64 tools and a Windows SDK. Run \`CHECK_CPP_TOOLCHAIN.bat\` first if the compiler or SDK is not detected, then run \`BUILD_CPP_APP.bat\`.

For hardware changes, record the exact cooler model, USB VID/PID, PM/SUB response, Windows version and build output. Test both a static image (including its periodic keep-alive behaviour) and at least one animated GIF. Check the system tray, startup option and disconnect/exit path after changes to app lifecycle code.

## Pull request guidance

- Explain the problem and the proposed change.
- List the files changed and any new build requirements.
- Include reproducible test steps and actual results.
- Distinguish measured behaviour from expected behaviour.
- Do not commit generated executables, object files, debug symbols, temporary GIFs or personal settings files.
- Keep commits focused and use descriptive messages.

## Hardware protocol changes

Treat HID packet layout as a compatibility-sensitive area. Document the change in \`docs/PROTOCOL.md\`, preserve the known BA120 identity checks, and do not send speculative packets to devices that have not passed the expected VID/PID and handshake validation.

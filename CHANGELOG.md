# Changelog

## v3.0.8

- Documented the native C++/Win32 application and BA120 Vision device profile.
- Added a build-time extraction step for the built-in Windows Shell32 Monitor and Keyboard icon.
- Includes x64 MSVC build and toolchain diagnostic scripts.
- Records the observed static-image refresh requirement and GIF frame-rate behaviour.

## v3.0.7 and earlier v3 prototypes

- Native C++ implementation work focused on reducing managed-runtime overhead.
- Added BA120 HID enumeration, handshake validation and RGB565 Type-2 frame transfer.
- Added still-image and animated GIF selection, scaling, rotation and preview.
- Added tray operation, saved preferences and optional Windows startup.
- Removed the \`hidparse.lib\` linker dependency by using the established HID input buffer size and validating the returned report length.

This changelog is a summary of the project history discussed during development. It is not a claim that every intermediate version was independently built or tested on every supported Windows release.

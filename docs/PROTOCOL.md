# BA120 Vision Type-2 protocol notes

This document records the device values and frame layout used by the current Native C++ v3 implementation. The values were established during development and physical testing of the user's Thermalright Burst Assassin 120 Vision display. They are hardware-profile details, not a public specification for all Thermalright LCDs.

## Device identity

- USB Vendor ID (VID): \`0x0416\`
- USB Product ID (PID): \`0x5302\`
- Panel mode (PM): \`52\` / \`0x34\`
- Sub-mode (SUB): \`0\`
- Transport: Windows HID, Type-2 profile
- Windows output report length: 513 bytes, including the leading HID report-ID byte

The application allocates a 513-byte input buffer for \`ReadFile\`. The transferred byte count is checked when validating the identity response. This avoids a link-time dependency on \`HidP_GetCaps\` and \`hidparse.lib\`, which was unavailable in the desktop SDK toolchain used for this project. The app must not assume that the same buffer length or profile applies to unrelated devices.

If the device interface path includes an \`&MI_\` suffix, only interface \`&MI_00\` is accepted by the current target profile.

## Handshake

The application sends a 513-byte HID output report with report ID \`0\`. The protocol payload begins after that report-ID byte.

| Field | Value |
|---|---|
| Protocol signature | \`DA DB DC DD\` |
| Ready-request marker | \`01\` at protocol offset 12 |
| HID report byte positions for signature | 1–4 |
| HID report byte position for ready marker | 13 |

The response must contain the same signature, SUB \`0\`, PM \`52\`, and ready marker \`01\` at protocol offset 12. Some returned reports include a leading zero report-ID byte; the application accounts for it before validating the protocol fields.

## Image payload

- Physical panel raster: 240 × 320 pixels
- Logical image canvas: 320 × 240 pixels before orientation conversion
- Pixel format: RGB565, little-endian
- Pixel payload: 240 × 320 × 2 = 153,600 bytes
- Protocol packet size: 512 bytes
- First packet: 20-byte protocol header followed by 492 pixel bytes
- Remaining data: 300 further protocol packets
- Windows HID output report: 1-byte report ID + 512-byte protocol packet
- Total per full frame: 301 HID output reports

The first packet contains these fields at protocol offsets:

| Protocol offset | Contents |
|---:|---|
| 0–3 | Signature \`DA DB DC DD\` |
| 4 | Type-2 frame command \`02\` |
| 6 | RGB565 mode \`01\` |
| 8–9 | Width 240, little-endian |
| 10–11 | Height 320, little-endian |
| 12 | Frame marker \`02\` |
| 16–19 | Payload length 153600, little-endian |
| 20–511 | First 492 pixel bytes |

Tail packets contain the remaining pixel bytes beginning at protocol offset 0. The HID report-ID byte remains outside the 512-byte protocol packet at report byte 0.

## Observed static-image refresh behaviour

During physical testing of the target BA120 unit, a static image reverted to the factory display roughly three seconds after the last HID transfer. The application therefore re-sends the already encoded frame packet sequence every two seconds. This is an empirically observed keep-alive behaviour, not a documented vendor command. No separate minimal keep-alive packet has been established.

## Compatibility warning

Do not send these reports to unknown HID devices. The application should validate the target VID/PID and PM/SUB handshake before transferring image data. Other Thermalright cooler displays may use different report lengths, panel dimensions, protocol commands, frame formats or firmware behaviour.

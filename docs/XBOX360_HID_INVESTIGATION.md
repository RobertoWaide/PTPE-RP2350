# Xbox 360 Traptanium HID investigation

## Goal and scope

This branch keeps the RP2350/Pico 2 W firmware and its Wi-Fi web interface, while improving the USB HID response path for direct connection to an Xbox 360. It is an experimental test build, not a confirmed Xbox-compatible release.

## Findings grounded in source

- Xenia Canary PR #1157 documents the Xbox 360 Traptanium Portal VID:PID as `1430:1F17`, uses IN endpoint `0x81` and OUT endpoint `0x02`, reads reports asynchronously, and sends 32-byte zero-padded output reports. Its Traptanium identification parser recognizes a packet beginning `0B 14 52`, with the portal ID bytes `02 1B` after that prefix. This is evidence from Xenia's PC/WinUSB integration and a physical portal test, not a USB trace of an Xbox 360 console.
- The inherited PTPE HID report descriptor describes 32-byte Input and Output reports. The endpoint descriptors advertise a maximum packet size of 64 bytes. That is not automatically invalid: the endpoint's maximum packet capacity can be larger than the HID report size. The same upstream descriptor values are retained rather than changed without a real-device descriptor capture.
- TinyUSB documents that `tud_hid_get_report_cb()` returning zero stalls an otherwise supported GET_REPORT request. The original PTPE callback does this too. The Xbox has not been observed issuing GET_REPORT in our test, so support added here is a compatibility improvement to test, not a proven root cause.

## Changes in this branch

1. Queue outgoing portal replies in an eight-entry ring buffer. Send only while the HID IN endpoint reports ready, and keep a reply queued if the endpoint refuses a transfer. The packet is kept at exactly 32 bytes and zero-padded, with the `0B 14` framing prefix.
2. Answer Input GET_REPORT requests with the last complete 32-byte portal frame (initial frame `0B 14` followed by zeros). Unsupported report IDs/types still return zero; this does not invent Feature reports absent from the HID descriptor.
3. Make verbose per-packet UART diagnostics opt-in using the CMake option `-DPTPE_USB_DIAGNOSTICS=ON`. Logging is disabled by default to avoid delaying responses when the firmware is running at USB polling rates.
4. Preserve the web UI, virtual Sense toggle, RAM dump upload, VID:PID `1430:1F17`, endpoint addresses, HID report descriptor and endpoint packet-size settings pending stronger evidence.

## What remains unproven

- Whether the Xbox 360 sends GET_REPORT for this device and which setup parameters it uses.
- Whether its enumeration accepts our exact device/configuration/string descriptors.
- Whether every response command/payload is correct for the Traptanium Portal.
- Whether the Xbox 360 game recognizes this firmware. CI compile success alone cannot establish hardware compatibility.

## Validation plan

1. Confirm that the Pico 2 W boots, hosts `PTPE-Skylanders`, and serves `http://192.168.4.1`.
2. Connect the Pico directly to the Xbox 360 and record whether the game's portal error changes.
3. If it still fails, build a diagnostic image with `-DPTPE_USB_DIAGNOSTICS=ON` and capture UART output with a 3.3 V USB-to-UART adapter. The Pico's USB cable is used for device emulation, so its USB connection does not itself expose the UART logs.
4. The next high-value evidence is a descriptor/protocol capture from a real Xbox 360 Traptanium Portal or a hardware USB analyzer on the console path.

## References

- Xenia Canary PR #1157: https://github.com/xenia-canary/xenia-canary/pull/1157
- Upstream PTPE: https://github.com/NicoAICP/PTPE
- TinyUSB HID callbacks: https://github.com/hathach/tinyusb/blob/master/src/class/hid/hid_device.h

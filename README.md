# Zalo10

![Version](https://img.shields.io/badge/version-1.5.0.0-blue)
![License](https://img.shields.io/badge/license-GPL%20v3.0-green)
![Platform](https://img.shields.io/badge/platform-BlackBerry%2010-orange)

**Native Zalo Client for BlackBerry 10**

Zalo10 is a Zalo messaging client built entirely with Cascades (Native C++/QML), delivering the smoothest experience for BlackBerry 10.

> **Note:** The Zalo app for Android 4.3 has been dead since October-November 2024. Zalo10 is the replacement for BB10 users.

<p align="center">
  <img src="splash1280x720.png" alt="Zalo10 splash screen" width="720">
</p>

---

## Requirements

- **OS:** BlackBerry 10.3.x or higher (10.3.3 recommended)
- **Device:** Must be rooted

---

## Installation Guide

1. Enable **Development Mode** on your BB10 device
2. Download the `.bar` file to your computer
3. Use **Sachesi**, **DBBT**, or **Bar Installer** to sideload the `.bar` file to your phone
4. Open the app, scan the QR code from Zalo on your main phone, and enjoy!

---

## Notes

- **Login:** Currently uses web API / QR code scanning from the official Zalo app on another phone
- **Menu:** To access Menu / Email / Info, swipe down with **1 fingers from the top edge of the screen**
- Zalo10 is an **unofficial** app, not affiliated with Zalo Official

---

## Third-Party Libraries & Credits

Zalo10 stands on the shoulders of these projects:

| Project | Used for | License |
|---|---|---|
| [BB10 root](https://bb10.root.sx/) by Oleksandr | Providing the rooting solution that enables the installation and development of unsigned applications once again. | - |
| [zca-js](https://github.com/RFS-ADRENO/zca-js) by RFS-ADRENO | Reference for the Zalo Web protocol (API endpoints, request signing and encryption, WebSocket frames). Not bundled: its logic is reimplemented in C++/Qt. | MIT |
| [XZ Embedded](https://tukaani.org/xz/embedded.html) | Decompressing the XZ stream in phone message sync (bundled in `src/third_party/xz`) | 0BSD |
| [libwebp](https://github.com/webmproject/libwebp) | Decoding WebP stickers (decoder-only static build in `src/third_party/webp`) | BSD-3-Clause |
| [OpenSSL](https://www.openssl.org/) | AES/RSA/MD5 and the TLS 1.2 connections (WebSocket, phone sync), from the BB10 NDK | OpenSSL License |
| [SQLite](https://www.sqlite.org/) | Local message and contact database, from the BB10 NDK | Public domain |
| [zlib](https://zlib.net/) | Inflating compressed WebSocket frames and HTTP responses | zlib License |
| [Qt 4.8](https://doc.qt.io/archives/qt-4.8/) and the BlackBerry 10 Cascades framework | UI, networking, JSON parsing | LGPL / BlackBerry SDK terms |

Thanks to the authors of all these projects.

---

## Support Development

If you're feeling generous, you can donate to help us afford test devices instead of relying on the simulator. We're paid with trust and hope ;-;

(See the About section in the app for more details)

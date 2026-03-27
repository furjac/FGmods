# 🔐 FGmods – Android License Verification System

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Android](https://img.shields.io/badge/Android-NDK%20r23+-green.svg)](https://developer.android.com/ndk)

A robust, native-layer license verification framework for Android applications. FGmods offloads critical security operations (cryptography, anti-tampering, server coordination) to a secure native library, making it resistant to Java-level exploitation. The intuitive UI frontend dynamically syncs strings from your backend, ensuring a polished user experience.

---

## Features

- per app login (if giving 2 different app to same person the same key won't work)

- per app update ota

- every text is customisable 

- firebase Realtime Database 

- can't be modded through java

- allow upto n number of users per key

- auto fetch of everything from firebase no need to restart app

- every text and url are encrypted 

- anti time travel (users can't change time and date to cheat )

---

## 🚀 Key Features

- **Native-Layer Security**  
  Sensitive strings (URLs, API paths, keys) are XOR-encrypted at compile time and decrypted only at runtime. All cryptographic operations (AES-256-CBC, PBKDF2, SHA-256) use statically-linked OpenSSL.

- **Anti-Debug & Tamper Protection**  
  Runtime checks detect `ptrace` attachment, `TracerPid` modifications, and kernel anomalies. Optional APK signature validation prevents repackaging (requires release certificate).

- **Device-Bound Licensing**  
  License keys bind to device fingerprints using `ANDROID_ID`, with support for multi-device limits and automatic enrollment.

- **Server-Side Device Bans**  
  Blacklist compromised or abusive devices via Firebase. Ban checks execute before any other verification logic.

- **Over-the-Air (OTA) Updates**  
  Fetch per-app version configs from Firebase and present native update dialogs with forced/optional flags.

- **Dynamic UI Strings**  
  All dialog text (titles, buttons, errors) is cached locally and refreshed from Firebase, eliminating hardcoded strings.

- **Encrypted Local Storage**  
  License keys are AES-encrypted with device-derived keys and persisted in `SharedPreferences`.

- **Modern, Customizable UI**  
  Dark theme with smooth animations and rounded corners. Fully configurable via server-side JSON.

---

## 📁 Project Structure

```
FGmods/
├── app/                               # Android application module
│   ├── src/main/java/fg/fgmods/key/
│   │   ├── fgmods.java                # Main gateway UI and entry point
│   │   ├── MainActivity.java          # Example integration activity
│   │   ├── NativeNetHelper.java       # HTTP bridge to native layer
│   │   └── NativePrefsHelper.java     # SharedPreferences bridge to native layer
│   ├── src/main/cpp/
│   │   ├── native-lib.cpp             # Core security and license logic
│   │   └── CMakeLists.txt             # Builds fgaccess.so library
│   └── openssl/                       # Pre-built OpenSSL static libraries per ABI
├── encrypt.py                         # String encryption utility
└── README.md
```

---

## 🛠 Prerequisites

- **Android Studio** with Gradle
- **Android NDK** (r23 or newer)
- **OpenSSL static libraries** for all target ABIs (arm64-v8a, armeabi-v7a, x86, x86_64)
  - Place libraries in `openssl/<ABI>/lib/`
  - Place headers in `openssl/include/`

---

## 🔨 Quick Start: Building

### 1. Prepare OpenSSL Libraries

Download or build OpenSSL static libraries for Android:
- Reference: [openssl-ndk](https://github.com/amitshekhariitbhu/openssl-ndk)
- Docs: [OpenSSL Android Build](https://github.com/openssl/openssl/blob/master/NOTES-ANDROID.md)

Copy `libcrypto.a` and `libssl.a` to the respective ABI subdirectories.

### 2. Configure Encrypted Strings

Edit `encrypt.py` with your Firebase endpoints:

```python
STRINGS = {
    "URL":        "https://your-project.firebaseio.com/",
    "BANS":       "/bans/",
    "KEYS":       "/keys/",
    "DEVICES":    "/devices/",
    "CFG_UPDATE": "/config/update.json",
    "CFG_UI":     "/config/ui_texts.json",
}
```

Run the encryption script:

```bash
python3 encrypt.py
```

This outputs C++ array declarations. Copy these into `native-lib.cpp`:

```cpp
// Original: 'https://your-project.firebaseio.com/'
static const unsigned char URL_ENC[] = { ... };
static const unsigned char URL_KEY[] = { ... };
```

### 3. Build the APK

Open the project in Android Studio and build. CMake automatically compiles the native library.

---

## 📦 Integration Guide

### Java/Kotlin

Import the gateway and invoke it early in your main Activity:

```java
import fg.fgmods.key.fgmods;

public class MainActivity extends AppCompatActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // Initialize FGmods license check (blocks until verification completes)
        fgmods gateway = new fgmods();
        gateway.showGateway(this);
    }
}
```

### Smali (APK Patching)

Insert the following into the target Activity's `onCreate` method after `invoke-super`:

```smali
new-instance v0, Lfg/fgmods/key/fgmods;
invoke-direct {v0}, Lfg/fgmods/key/fgmods;-><init>()V
move-object p0, p0
invoke-virtual {v0, p0}, Lfg/fgmods/key/fgmods;->showGateway(Landroid/app/Activity;)V
```

Ensure the `fg/fgmods/key/` package and dependencies are included in the modified APK.

---

## 🔐 Firebase Database Schema

Your Firebase Realtime Database should follow this structure (paths are configurable via encrypted strings):

### Device Ban List
**Path:** `/bans/<hashedDeviceFingerprint>.json`

```json
{
  "reason": "Violation of terms of service."
}
```

If a device hash exists here, access is denied immediately.

### License Key Registry
**Path:** `/keys/<licenseKey>.json`

```json
{
  "expirydate": 1767196800000,
  "app_package": "com.example.app",
  "max_devices": 3,
  "devices": "hash1,hash2"
}
```

| Field | Description |
|-------|-------------|
| `expirydate` | Unix timestamp (ms) when the license expires |
| `app_package` | (Optional) Package name; restricts key to one app |
| `max_devices` | Maximum devices allowed to bind this key |
| `devices` | Comma-separated hashes of already-bound devices |

### Device Registry
**Path:** `/devices/<hashedDeviceFingerprint>.json`

```json
{
  "deviceId": "hash_value",
  "model": "Pixel 6",
  "manufacturer": "Google",
  "lastSeen": 1700000000000,
  "key": "license_key_used",
  "appPackage": "com.example.app"
}
```

Created after successful license verification; updated on each check-in.

### UI Text Configuration
**Path:** `/config/ui_texts.json`

```json
{
  "title": "🔐 VAULT ACCESS",
  "subtitle": "Enter license key to unlock features",
  "key_hint": "fgmods_XXXX-XXXX-XXXX",
  "verify_button": "VERIFY LICENSE",
  "contact_button": "CONTACT ADMIN",
  "auto_title": "Verifying saved session...",
  "locked_title": "Too Many Attempts",
  "locked_subtitle": "Locked for 15 minutes.",
  "ban_title": "Device Banned",
  "ban_subtitle": "Reason: %s",
  "appeal_button": "APPEAL BAN (TELEGRAM)",
  "update_title": "Update Available",
  "update_now": "Update Now",
  "update_download_progress": "Downloading update...",
  "error_title": "Connection Error",
  "network_error": "Failed to verify device status.",
  "retry": "Retry",
  "exit": "Exit",
  "error_key_not_found": "License key not found.",
  "error_expired": "License has expired.",
  "error_wrong_app": "This key is not valid for this app.",
  "error_device_limit": "Device limit reached for this key.",
  "error_connection": "Database connection failed.",
  "grant_toast": "Access Granted"
}
```

Fetched at runtime; cached version used on network failure.

### OTA Update Configuration
**Path:** `/config/update_<package_name>.json` (replace `.` with `_`)

```json
{
  "version_code": 2,
  "download_url": "https://example.com/app-v2.apk",
  "update_message": "Critical security patches included.",
  "force_update": true
}
```

Example: `com.example.app` → `/config/update_com_example_app.json`

---

## 🛡 Security Architecture

| Feature | Implementation |
|---------|-----------------|
| **String Obfuscation** | XOR encryption with embedded key; decrypted only at runtime |
| **Anti-Debug** | `ptrace` detection, `TracerPid` inspection, kernel state checks |
| **Cryptography** | Static OpenSSL linking (no system dependency); AES-256-CBC with PBKDF2 |
| **JNI Hardening** | Minimal Java ↔ Native calls; all logic in C++ to prevent Frida/Xposed hooks |
| **Brute-Force Protection** | 10-attempt lockout (15 minutes, configurable) |
| **Device ID** | `ANDROID_ID` with fallback; no extra permissions required |

---

## 💬 Support & Contributing

**Questions?** Open an issue or contact me on Telegram: [t.me/furjack](https://t.me/furjack)

**Contributing?** PRs are welcome—please prioritize security hardening and native-layer improvements.

---

## 📄 License

This project is provided as-is for educational and commercial integration. You retain full responsibility for compliance with its dependencies' licenses (OpenSSL, etc.). The author disclaims liability for misuse.

---

## 💝 Support This Project

If FGmods saves you time or improves your security posture, consider supporting continued development:

[![Donate via PayPal](https://img.shields.io/badge/Donate-PayPal-blue.svg)](https://paypal.me/fgmodss)

Your contributions help keep the project maintained and improved!

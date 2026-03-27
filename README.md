# 🔐 FGmods – Android License Verification System

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Android](https://img.shields.io/badge/Android-NDK%20r23+-green.svg)](https://developer.android.com/ndk)

**FGmods** is a secure, native‑layer license verification framework for Android applications. It offloads all critical logic (crypto, anti‑tampering, network coordination) to a native `.so` library, making it resistant to common Java‑level patching techniques. The Java front‑end is a clean, modern UI shell with dynamic text fetched from your server.

---

## 🚀 Features

- **Native‑Layer Security**  
  All sensitive data (URLs, paths, keys) is XOR‑encrypted in the binary and decrypted only at runtime.  
  Crypto (AES‑256‑CBC, PBKDF2, SHA‑256) uses statically linked OpenSSL.

- **Anti‑Debug & Tamper Detection**  
  Library checks for `ptrace`, `TracerPid`, and suspicious kernel wait channels at load time.  
  (Sign‑based APK validation is prepared – you must supply your release certificate.)

- **Device‑Bound Licensing**  
  Keys are bound to a device fingerprint (`ANDROID_ID`). Supports multi‑device limits and automatic binding.

- **Server‑Side Ban Management**  
  Banned device fingerprints are stored in Firebase. The native library checks this before any other operation.

- **Over‑the‑Air (OTA) Updates**  
  Per‑app update configs (version, download URL, forced flag) are fetched from Firebase and presented in a native dialog.

- **Dynamic UI Text**  
  All dialog strings (titles, buttons, error messages) are fetched from a Firebase JSON file and cached locally.

- **Secure Storage**  
  License keys are encrypted with a device‑derived AES key and stored in `SharedPreferences`.

- **Clean, Modern UI**  
  Dark theme, rounded corners, smooth animations – fully customisable via server‑side JSON.

---

## 📁 Project Structure

```

FGmods/
├── app/                               (Android application module)
│   ├── src/main/java/fg/fgmods/key/
│   │   ├── fgmods.java                – UI shell, entry point
│   │   ├── MainActivity.java          – example Activity
│   │   ├── NativeNetHelper.java       – HTTP bridge for native code
│   │   └── NativePrefsHelper.java     – SharedPreferences bridge
│   ├── src/main/cpp/
│   │   ├── native-lib.cpp             – core native logic
│   │   └── CMakeLists.txt             – builds fgaccess.so
│   └── openssl/                       – static OpenSSL libraries (per ABI)
├── encrypt.py                         – helper script to encrypt strings
└── README.md

```

---

## 🛠 Prerequisites

- Android Studio / Gradle
- NDK (r23+ recommended)
- OpenSSL prebuilt static libraries for all target ABIs (arm64-v8a, armeabi-v7a, x86, x86_64)  
  Place them in `openssl/<ABI>/lib/` and headers in `openssl/include/`.

---

## 🔨 Building the Native Library

### 1. Obtain OpenSSL

Download or build static libraries for Android.  
Example using [openssl‑ndk](https://github.com/amitshekhariitbhu/openssl-ndk) or follow the [OpenSSL Android instructions](https://github.com/openssl/openssl/blob/master/NOTES-ANDROID.md).  
After building, copy the `libcrypto.a` and `libssl.a` files into the corresponding ABI folders inside `openssl/`.

### 2. Encrypt your constants

The native code uses XOR‑encrypted strings. The provided `encrypt.py` script generates the byte arrays you need to paste into `native-lib.cpp`.

Edit `encrypt.py` and set your actual Firebase database URL and paths:

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

Run the script:

```bash
python3 encrypt.py
```

It will output C++ array declarations like:

```cpp
// Original: 'https://your-project.firebaseio.com/'
static const unsigned char URL_ENC[] = { ... };
static const unsigned char URL_KEY[] = { ... };
```

Copy this block and replace the placeholder definitions in native-lib.cpp (the sections marked URL_ENC, URL_KEY, etc.).

3. Build the APK

Open the project in Android Studio and build it. The native library will be compiled via CMake.

---

📦 Integrating FGmods into Your App

Java / Kotlin Integration

Add the fg.fgmods.key package and the native library to your project. In your main Activity, simply create an instance and call showGateway():

```java
import fg.fgmods.key.fgmods;

public class MainActivity extends AppCompatActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // ... your setup ...

        fgmods gateway = new fgmods();
        gateway.showGateway(this);  // This blocks until license is verified
    }
}
```

Smali Integration (for modded apps)

If you are patching an existing APK, you can insert the call to showGateway in the target Activity's onCreate method. Use this smali code:

```smali
new-instance v0, Lfg/fgmods/key/fgmods;

invoke-direct {v0}, Lfg/fgmods/key/fgmods;-><init>()V

move-object p0, p0

invoke-virtual {v0, p0}, Lfg/fgmods/key/fgmods;->showGateway(Landroid/app/Activity;)V
```

Insert this after the invoke-super call in onCreate. Make sure to add the fg/fgmods/key/fgmods class (and its dependencies) to the APK.

---

🔐 Firebase Database Schema

FGmods expects your Firebase Realtime Database to be structured as follows (paths are configurable via encrypted strings):

/bans/<hashedDeviceFp>.json

```json
{ "reason": "Violation of terms." }
```

If present, the device is banned.

/keys/<rawKey>.json

```json
{
  "expirydate": 1767196800000,
  "app_package": "com.example.app",
  "max_devices": 3,
  "devices": "hash1,hash2"
}
```

· expirydate – milliseconds since epoch.
· app_package – optional, restricts key to one app.
· max_devices – how many devices can bind this key.
· devices – comma‑separated list of hashed device fingerprints.

/devices/<hashedDeviceFp>.json

```json
{
  "deviceId": "hash",
  "model": "Pixel 6",
  "manufacturer": "Google",
  "lastSeen": 1700000000000,
  "key": "the raw key used",
  "appPackage": "com.example.app"
}
```

Created after successful verification.

/config/ui_texts.json

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
  "network_error": "Failed to verify device status...",
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

All UI texts are fetched at runtime; if the network fails, the cached version is used.

/config/update_<package_name>.json

```json
{
  "version_code": 2,
  "download_url": "https://example.com/update.apk",
  "update_message": "A new version is available.",
  "force_update": true
}
```

Used for OTA updates. Replace . in package name with _ (e.g., com_example_app).

---

🛡 Security Considerations

· String Obfuscation
    All sensitive strings are XOR‑encrypted with a key stored separately in the binary. This makes static analysis harder.
· Anti‑Debug
    ptrace and TracerPid checks are performed early. You may extend checkSignature() with your own APK signature validation.
· Native Crypto
    OpenSSL is statically linked, reducing dependency on system libraries. AES‑256‑CBC with PBKDF2 key derivation is used.
· JNI Isolation
    The Java layer only calls a handful of native methods; all business logic stays in C++. This prevents easy hooking with tools like Frida or Xposed.
· Lockout Mechanism
    After 10 failed attempts, the device is locked for 15 minutes (configurable).
· Device Fingerprint
    Uses ANDROID_ID (falls back to "unknown_id" if unavailable). No permissions required.

---

💝 Support the Project

If you find FGmods useful and want to support further development, consider a donation:

https://img.shields.io/badge/Donate-PayPal-blue.svg

Your support helps keep the project maintained and improved!

---

📄 License

This project is provided as‑is for educational and integration purposes. You are free to use it in your own applications, but you must ensure compliance with the licenses of its dependencies (OpenSSL, etc.). The author assumes no liability for misuse.

---

🤝 Contributing

Pull requests and issues are welcome. Please keep the native‑layer security improvements in mind when contributing.

---

📞 Support

For questions or customisation, contact the project maintainer on Telegram.

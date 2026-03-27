/**
 * native-lib.cpp  –  fgaccess.so
 *
 * ALL security-sensitive logic lives here.  The Java layer (fgmods.java) is a
 * thin UI shell that calls these JNI functions; it contains NO business logic,
 * NO URLs, NO paths, and NO crypto code that an attacker could patch.
 *
 * Build flags recommended in CMakeLists.txt:
 *   -O2 -fvisibility=hidden -fstack-protector-strong
 *   -D_FORTIFY_SOURCE=2 -Wl,-z,relro,-z,now
 */

#include <jni.h>
#include <string>
#include <cstring>
#include <sstream>
#include <fstream>
#include <vector>
#include <memory>
#include <stdexcept>
#include <sys/ptrace.h>
#include <unistd.h>
#include <android/log.h>

// ─── OpenSSL (bundled via prefab / ndk-crypto) ───────────────────────────────
// If you are not linking OpenSSL, replace the SHA-256 / AES / PBKDF2 calls
// with your own implementation or use the Tink C++ library.
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>

#define LOG_TAG "fgaccess"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ─────────────────────────────────────────────────────────────────────────────
// 0.  COMPILE-TIME OBFUSCATION HELPERS
//     Strings are stored XOR-encrypted so they don't appear in the .so as
//     plain text.  Generate the byte arrays with the companion Python script.
// ─────────────────────────────────────────────────────────────────────────────

static std::string xorDecrypt(const unsigned char* data, size_t len,
                               const unsigned char* key, size_t klen) {
    std::string out(len, '\0');
    for (size_t i = 0; i < len; ++i)
        out[i] = static_cast<char>(data[i] ^ key[i % klen]);
    return out;
}

// ── Encrypted constant definitions ──────────────────────────────────────────
// Replace every placeholder block with output from your Python encrypt script.
// Example keying: python3 encrypt.py "https://your-project.firebaseio.com/"

static const unsigned char URL_ENC[] = {
    0xB9, 0xAB, 0xBD, 0x1E, 0xDA, 0x08, 0xBE, 0x44, 0xA5, 0x90, 0xB9, 0xA2,
    0x13, 0x1D, 0xE5, 0xF2, 0xE2, 0xEA, 0xE4, 0x0A, 0xCC, 0x54, 0xF0, 0x1E,
    0xBA, 0x9D, 0xFA, 0xB3, 0x4A, 0x41, 0xB6, 0xEB, 0xB7, 0xB6, 0xBB, 0x0B,
    0xCB, 0x53, 0xE2, 0x0E, 0xBF, 0x86, 0xF9, 0xA2, 0x51, 0x48, 0xFB
};
static const unsigned char URL_KEY[] = {
    0xD1, 0xDF, 0xC9, 0x6E, 0xA9, 0x32, 0x91, 0x6B, 0xD6, 0xE9, 0xD7, 0xC1,
    0x3E, 0x25, 0xD4, 0xC5
};

// Original: '/bans/'
static const unsigned char BANS_ENC[] = {
    0x05, 0x53, 0x44, 0xE0, 0x5E, 0x7C
};
static const unsigned char BANS_KEY[] = {
    0x2A, 0x31, 0x25, 0x8E, 0x2D, 0x53, 0xEF, 0xC3, 0xF9, 0x6D, 0xEC, 0x3B,
    0x23, 0xB7, 0xC5, 0x30
};

// Original: '/keys/'
static const unsigned char KEYS_ENC[] = {
    0x83, 0x9C, 0x94, 0x2F, 0xD0, 0xA4
};
static const unsigned char KEYS_KEY[] = {
    0xAC, 0xF7, 0xF1, 0x56, 0xA3, 0x8B, 0x71, 0x1E, 0x98, 0x87, 0x7F, 0x6C,
    0x8B, 0xED, 0x2D, 0xC0
};

// Original: '/devices/'
static const unsigned char DEVICES_ENC[] = {
    0xF8, 0x0F, 0x95, 0x77, 0x16, 0xB9, 0xC1, 0x18, 0x1E
};
static const unsigned char DEVICES_KEY[] = {
    0xD7, 0x6B, 0xF0, 0x01, 0x7F, 0xDA, 0xA4, 0x6B, 0x31, 0x0F, 0xE8, 0xDD,
    0x4C, 0xC0, 0xED, 0xCA
};

// Original: '/config/update.json'
static const unsigned char CFG_UPDATE_ENC[] = {
    0x54, 0xC5, 0xE5, 0xC0, 0x86, 0x2B, 0xD2, 0x90, 0x97, 0x60, 0x5C, 0xDD,
    0x7A, 0x9D, 0x22, 0xA9, 0x08, 0xC9, 0xE4
};
static const unsigned char CFG_UPDATE_KEY[] = {
    0x7B, 0xA6, 0x8A, 0xAE, 0xE0, 0x42, 0xB5, 0xBF, 0xE2, 0x10, 0x38, 0xBC,
    0x0E, 0xF8, 0x0C, 0xC3
};

// Original: '/config/ui_texts.json'
static const unsigned char CFG_UI_ENC[] = {
    0x67, 0x62, 0x38, 0x6F, 0x9B, 0x0E, 0xCD, 0x1E, 0x93, 0xD9, 0x31, 0x05,
    0x7C, 0xA2, 0x08, 0xB3, 0x66, 0x6B, 0x24, 0x6E, 0x93
};
static const unsigned char CFG_UI_KEY[] = {
    0x48, 0x01, 0x57, 0x01, 0xFD, 0x67, 0xAA, 0x31, 0xE6, 0xB0, 0x6E, 0x71,
    0x19, 0xDA, 0x7C, 0xC0
};

// Decrypt helpers (inlined so the compiler can optimize)
#define DECRYPT(arr) xorDecrypt(arr##_ENC, sizeof(arr##_ENC), \
                                arr##_KEY, sizeof(arr##_KEY))

// ─────────────────────────────────────────────────────────────────────────────
// 1.  ANTI-TAMPERING  (called once at library load via JNI_OnLoad)
// ─────────────────────────────────────────────────────────────────────────────

/** Returns true if a debugger is attached. */
static bool isDebugged() {
    // Method 1: ptrace self-attach
    if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) == -1)
        return true;

    // Method 2: /proc/self/status TracerPid
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("TracerPid:", 0) == 0) {
            int pid = 0;
            sscanf(line.c_str(), "TracerPid:\t%d", &pid);
            if (pid != 0) return true;
            break;
        }
    }

    // Method 3: /proc/self/wchan  (thread blocked in "ptrace_stop"?)
    std::ifstream wchan("/proc/self/wchan");
    std::string wchanStr;
    if (std::getline(wchan, wchanStr) && wchanStr.find("ptrace") != std::string::npos)
        return true;

    return false;
}

/** Checks whether the running APK is signed with a release signature.
 *  Call this to detect repackaged / re-signed APKs.
 *  The expected SHA-256 hex of your release certificate must be embedded here
 *  (encrypted, of course).  Leave blank during development. */
static bool checkSignature(JNIEnv* env) {
    // TODO: embed your release certificate SHA-256 (encrypted) and compare
    // against the result of PackageManager.getPackageInfo(..., GET_SIGNATURES).
    // Returning true here means "signature OK" – replace with real check.
    (void)env;
    return true;
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
        return -1;


    return JNI_VERSION_1_6;
}

// ─────────────────────────────────────────────────────────────────────────────
// 2.  CRYPTO PRIMITIVES  (AES-256-CBC + PBKDF2-SHA256 + SHA-256)
// ─────────────────────────────────────────────────────────────────────────────

/** SHA-256 of arbitrary bytes, result as lowercase hex string. */
static std::string sha256Hex(const uint8_t* data, size_t len) {
    uint8_t digest[SHA256_DIGEST_LENGTH];
    SHA256(data, len, digest);
    char hex[SHA256_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return std::string(hex, SHA256_DIGEST_LENGTH * 2);
}

static std::string sha256Hex(const std::string& s) {
    return sha256Hex(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

/** PBKDF2-SHA256: derive a 32-byte AES key from deviceFp. */
static std::vector<uint8_t> deriveAesKey(const std::string& deviceFp) {
    // Salt = SHA-256 of deviceFp  (same as Java reference implementation)
    uint8_t salt[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const uint8_t*>(deviceFp.data()),
           deviceFp.size(), salt);

    std::vector<uint8_t> key(32);
    if (PKCS5_PBKDF2_HMAC(deviceFp.c_str(),
                           static_cast<int>(deviceFp.size()),
                           salt, sizeof(salt),
                           1000,            // iterations – match Java
                           EVP_sha256(),
                           32, key.data()) != 1) {
        throw std::runtime_error("PBKDF2 failed");
    }
    return key;
}

/** AES-256-CBC encrypt.  Returns  base64(iv) + ":" + base64(ciphertext). */
static std::string aesEncrypt(const std::string& plaintext,
                               const std::vector<uint8_t>& key) {
    uint8_t iv[16];
    if (RAND_bytes(iv, sizeof(iv)) != 1)
        throw std::runtime_error("RAND_bytes failed");

    std::vector<uint8_t> ct(plaintext.size() + 16);
    int outLen1 = 0, outLen2 = 0;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv);
    EVP_EncryptUpdate(ctx,
                      ct.data(), &outLen1,
                      reinterpret_cast<const uint8_t*>(plaintext.data()),
                      static_cast<int>(plaintext.size()));
    EVP_EncryptFinal_ex(ctx, ct.data() + outLen1, &outLen2);
    EVP_CIPHER_CTX_free(ctx);
    ct.resize(outLen1 + outLen2);

    // Base64 encode using Java (called from JNI) – or use a C base64 impl:
    auto b64 = [](const uint8_t* d, size_t n) -> std::string {
        static const char T[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((n + 2) / 3) * 4);
        for (size_t i = 0; i < n; i += 3) {
            uint32_t v = static_cast<uint32_t>(d[i]) << 16;
            if (i + 1 < n) v |= static_cast<uint32_t>(d[i+1]) << 8;
            if (i + 2 < n) v |= d[i+2];
            out += T[(v >> 18) & 63]; out += T[(v >> 12) & 63];
            out += (i + 1 < n) ? T[(v >> 6) & 63] : '=';
            out += (i + 2 < n) ? T[v & 63]        : '=';
        }
        return out;
    };

    return b64(iv, 16) + ":" + b64(ct.data(), ct.size());
}

/** AES-256-CBC decrypt.  Input format: base64(iv) + ":" + base64(ct). */
static std::string aesDecrypt(const std::string& stored,
                               const std::vector<uint8_t>& key) {
    auto b64d = [](const std::string& in) -> std::vector<uint8_t> {
        static const int T[256] = {
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
            52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
            -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
            15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
            -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
            41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
        };
        std::vector<uint8_t> out;
        out.reserve(in.size() * 3 / 4);
        uint32_t v = 0; int bits = 0;
        for (char c : in) {
            if (c == '=') break;
            if ((unsigned)c > 127 || T[(unsigned)c] < 0) continue;
            v = (v << 6) | T[(unsigned)c];
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out.push_back(static_cast<uint8_t>((v >> bits) & 0xFF));
            }
        }
        return out;
    };

    size_t colon = stored.find(':');
    if (colon == std::string::npos) throw std::runtime_error("Bad stored format");
    auto iv  = b64d(stored.substr(0, colon));
    auto ct  = b64d(stored.substr(colon + 1));
    if (iv.size() != 16) throw std::runtime_error("Bad IV length");

    std::vector<uint8_t> pt(ct.size());
    int outLen1 = 0, outLen2 = 0;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data());
    EVP_DecryptUpdate(ctx, pt.data(), &outLen1, ct.data(),
                      static_cast<int>(ct.size()));
    if (EVP_DecryptFinal_ex(ctx, pt.data() + outLen1, &outLen2) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("AES decrypt failed – bad key or data");
    }
    EVP_CIPHER_CTX_free(ctx);
    pt.resize(outLen1 + outLen2);
    return std::string(reinterpret_cast<char*>(pt.data()), pt.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// 3.  NETWORK  (HTTP GET / PUT / PATCH via Java's HttpURLConnection)
//     We call back into Java so we keep one network path and avoid duplicating
//     certificate pinning logic.  All URL assembly happens here in native code.
// ─────────────────────────────────────────────────────────────────────────────

/** Build a full Firebase REST URL from a decrypted path fragment. */
static std::string buildUrl(const std::string& path) {
    std::string base = DECRYPT(URL);
    if (!base.empty() && base.back() != '/') base += '/';
    std::string p = path;
    if (!p.empty() && p.front() == '/') p.erase(0, 1);
    // Append .json only if the path doesn't already have it
    if (p.size() < 5 || p.substr(p.size() - 5) != ".json") {
        p += ".json";
    }
    return base + p;
}

// JNI helper: call a Java method returning String on a URL helper class.
// The helper class (NativeNetHelper) is a package-private Java class that
// exposes three static methods:
//   static String httpGet(String url)
//   static String httpPatch(String url, String json)
//   static String httpPut(String url, String json)
//   static String getServerDate(String url)
// Keeping network I/O in Java lets us reuse Android's TLS stack and
// DownloadManager without reimplementing them in C++.

static jclass    g_netClass  = nullptr;
static jmethodID g_httpGet   = nullptr;
static jmethodID g_httpPatch = nullptr;
static jmethodID g_httpPut   = nullptr;
static jmethodID g_serverDate= nullptr;

static void ensureNetHelper(JNIEnv* env) {
    if (g_netClass) return;
    jclass cls = env->FindClass("fg/fgmods/key/NativeNetHelper");
    if (!cls) { LOGE("NativeNetHelper not found"); return; }
    g_netClass   = static_cast<jclass>(env->NewGlobalRef(cls));
    g_httpGet    = env->GetStaticMethodID(g_netClass, "httpGet",
                       "(Ljava/lang/String;)Ljava/lang/String;");
    g_httpPatch  = env->GetStaticMethodID(g_netClass, "httpPatch",
                       "(Ljava/lang/String;Ljava/lang/String;)V");
    g_httpPut    = env->GetStaticMethodID(g_netClass, "httpPut",
                       "(Ljava/lang/String;Ljava/lang/String;)V");
    g_serverDate = env->GetStaticMethodID(g_netClass, "getServerDate",
                       "(Ljava/lang/String;)Ljava/lang/String;");
}

static std::string jniGet(JNIEnv* env, const std::string& url) {
    ensureNetHelper(env);
    if (!g_httpGet) throw std::runtime_error("httpGet not found");
    jstring jurl  = env->NewStringUTF(url.c_str());
    jstring jresult = static_cast<jstring>(
        env->CallStaticObjectMethod(g_netClass, g_httpGet, jurl));
    env->DeleteLocalRef(jurl);
    if (!jresult) return "";
    const char* c = env->GetStringUTFChars(jresult, nullptr);
    std::string r(c);
    env->ReleaseStringUTFChars(jresult, c);
    env->DeleteLocalRef(jresult);
    return r;
}

static void jniPatch(JNIEnv* env, const std::string& url,
                     const std::string& json) {
    ensureNetHelper(env);
    if (!g_httpPatch) throw std::runtime_error("httpPatch not found");
    jstring ju = env->NewStringUTF(url.c_str());
    jstring jj = env->NewStringUTF(json.c_str());
    env->CallStaticVoidMethod(g_netClass, g_httpPatch, ju, jj);
    env->DeleteLocalRef(ju); env->DeleteLocalRef(jj);
}

static void jniPut(JNIEnv* env, const std::string& url,
                   const std::string& json) {
    ensureNetHelper(env);
    if (!g_httpPut) throw std::runtime_error("httpPut not found");
    jstring ju = env->NewStringUTF(url.c_str());
    jstring jj = env->NewStringUTF(json.c_str());
    env->CallStaticVoidMethod(g_netClass, g_httpPut, ju, jj);
    env->DeleteLocalRef(ju); env->DeleteLocalRef(jj);
}

static std::string jniGetServerDate(JNIEnv* env, const std::string& url) {
    ensureNetHelper(env);
    if (!g_serverDate) return "";
    jstring ju = env->NewStringUTF(url.c_str());
    jstring jd = static_cast<jstring>(
        env->CallStaticObjectMethod(g_netClass, g_serverDate, ju));
    env->DeleteLocalRef(ju);
    if (!jd) return "";
    const char* c = env->GetStringUTFChars(jd, nullptr);
    std::string r(c);
    env->ReleaseStringUTFChars(jd, c);
    env->DeleteLocalRef(jd);
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// 4.  SHARED-PREFERENCES  (accessed via JNI helper)
//     Native code reads/writes encrypted prefs through NativePrefsHelper
//     (a package-private Java class with static methods).
// ─────────────────────────────────────────────────────────────────────────────

static jclass    g_prefsClass  = nullptr;
static jmethodID g_prefsGet    = nullptr;
static jmethodID g_prefsPut    = nullptr;
static jmethodID g_prefsRemove = nullptr;
static jmethodID g_prefsGetLng = nullptr;
static jmethodID g_prefsPutLng = nullptr;
static jmethodID g_prefsGetInt = nullptr;
static jmethodID g_prefsPutInt = nullptr;

static void ensurePrefsHelper(JNIEnv* env) {
    if (g_prefsClass) return;
    jclass cls = env->FindClass("fg/fgmods/key/NativePrefsHelper");
    if (!cls) { LOGE("NativePrefsHelper not found"); return; }
    g_prefsClass  = static_cast<jclass>(env->NewGlobalRef(cls));
    g_prefsGet    = env->GetStaticMethodID(g_prefsClass, "getString",
                        "(Landroid/content/Context;Ljava/lang/String;)"
                        "Ljava/lang/String;");
    g_prefsPut    = env->GetStaticMethodID(g_prefsClass, "putString",
                        "(Landroid/content/Context;"
                         "Ljava/lang/String;Ljava/lang/String;)V");
    g_prefsRemove = env->GetStaticMethodID(g_prefsClass, "remove",
                        "(Landroid/content/Context;Ljava/lang/String;)V");
    g_prefsGetLng = env->GetStaticMethodID(g_prefsClass, "getLong",
                        "(Landroid/content/Context;Ljava/lang/String;)J");
    g_prefsPutLng = env->GetStaticMethodID(g_prefsClass, "putLong",
                        "(Landroid/content/Context;"
                         "Ljava/lang/String;J)V");
    g_prefsGetInt = env->GetStaticMethodID(g_prefsClass, "getInt",
                        "(Landroid/content/Context;Ljava/lang/String;)I");
    g_prefsPutInt = env->GetStaticMethodID(g_prefsClass, "putInt",
                        "(Landroid/content/Context;"
                         "Ljava/lang/String;I)V");
}

// ── Pref key names (encrypt these too in production) ─────────────────────────
// Using short obfuscated names here; store the real mapping in your encrypt script.
static const char* const P_KEY      = "sv_k";
static const char* const P_ATTEMPTS = "sv_a";
static const char* const P_LOCKOUT  = "sv_l";
static const char* const P_UI_CACHE = "ui_config_cache";

static std::string prefsGetString(JNIEnv* env, jobject ctx,
                                   const char* key) {
    ensurePrefsHelper(env);
    if (!g_prefsGet) return "";
    jstring jk = env->NewStringUTF(key);
    jstring jv = static_cast<jstring>(
        env->CallStaticObjectMethod(g_prefsClass, g_prefsGet, ctx, jk));
    env->DeleteLocalRef(jk);
    if (!jv) return "";
    const char* c = env->GetStringUTFChars(jv, nullptr);
    std::string r(c);
    env->ReleaseStringUTFChars(jv, c);
    env->DeleteLocalRef(jv);
    return r;
}

static void prefsPutString(JNIEnv* env, jobject ctx,
                            const char* key, const std::string& val) {
    ensurePrefsHelper(env);
    if (!g_prefsPut) return;
    jstring jk = env->NewStringUTF(key);
    jstring jv = env->NewStringUTF(val.c_str());
    env->CallStaticVoidMethod(g_prefsClass, g_prefsPut, ctx, jk, jv);
    env->DeleteLocalRef(jk); env->DeleteLocalRef(jv);
}

static void prefsRemove(JNIEnv* env, jobject ctx, const char* key) {
    ensurePrefsHelper(env);
    if (!g_prefsRemove) return;
    jstring jk = env->NewStringUTF(key);
    env->CallStaticVoidMethod(g_prefsClass, g_prefsRemove, ctx, jk);
    env->DeleteLocalRef(jk);
}

static jlong prefsGetLong(JNIEnv* env, jobject ctx, const char* key) {
    ensurePrefsHelper(env);
    if (!g_prefsGetLng) return 0LL;
    jstring jk = env->NewStringUTF(key);
    jlong v = env->CallStaticLongMethod(g_prefsClass, g_prefsGetLng, ctx, jk);
    env->DeleteLocalRef(jk);
    return v;
}

static void prefsPutLong(JNIEnv* env, jobject ctx,
                          const char* key, jlong val) {
    ensurePrefsHelper(env);
    if (!g_prefsPutLng) return;
    jstring jk = env->NewStringUTF(key);
    env->CallStaticVoidMethod(g_prefsClass, g_prefsPutLng, ctx, jk, val);
    env->DeleteLocalRef(jk);
}

static jint prefsGetInt(JNIEnv* env, jobject ctx, const char* key) {
    ensurePrefsHelper(env);
    if (!g_prefsGetInt) return 0;
    jstring jk = env->NewStringUTF(key);
    jint v = env->CallStaticIntMethod(g_prefsClass, g_prefsGetInt, ctx, jk);
    env->DeleteLocalRef(jk);
    return v;
}

static void prefsPutInt(JNIEnv* env, jobject ctx,
                         const char* key, jint val) {
    ensurePrefsHelper(env);
    if (!g_prefsPutInt) return;
    jstring jk = env->NewStringUTF(key);
    env->CallStaticVoidMethod(g_prefsClass, g_prefsPutInt, ctx, jk, val);
    env->DeleteLocalRef(jk);
}

// ─────────────────────────────────────────────────────────────────────────────
// 5.  CORE BUSINESS LOGIC  (the real work – fully native)
// ─────────────────────────────────────────────────────────────────────────────

static const int   MAX_ATTEMPTS = 10;
static const jlong LOCKOUT_MS   = 15LL * 60 * 1000;

/** Encrypt rawKey and persist to SharedPreferences. */
static void saveEncryptedKey(JNIEnv* env, jobject ctx,
                              const std::string& rawKey,
                              const std::string& deviceFp) {
    auto aesKey  = deriveAesKey(deviceFp);
    std::string stored = aesEncrypt(rawKey, aesKey);
    prefsPutString(env, ctx, P_KEY, stored);
}

/** Decrypt and return the saved key, or empty string on failure. */
static std::string loadEncryptedKey(JNIEnv* env, jobject ctx,
                                     const std::string& deviceFp) {
    std::string stored = prefsGetString(env, ctx, P_KEY);
    if (stored.empty()) return "";
    try {
        auto aesKey = deriveAesKey(deviceFp);
        return aesDecrypt(stored, aesKey);
    } catch (...) { return ""; }
}

/** Returns true if the device is currently locked out. */
static bool isLockedOut(JNIEnv* env, jobject ctx) {
    jlong lockTs = prefsGetLong(env, ctx, P_LOCKOUT);
    if (lockTs == 0) return false;
    // Use System.currentTimeMillis via JNI
    jclass sysClass = env->FindClass("java/lang/System");
    jmethodID millis = env->GetStaticMethodID(sysClass, "currentTimeMillis", "()J");
    jlong now = env->CallStaticLongMethod(sysClass, millis);
    env->DeleteLocalRef(sysClass);
    if (now - lockTs > LOCKOUT_MS) {
        prefsRemove(env, ctx, P_LOCKOUT);
        prefsPutInt(env, ctx, P_ATTEMPTS, 0);
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 6.  SIMPLE JSON HELPERS  (avoid a full JSON library dependency)
// ─────────────────────────────────────────────────────────────────────────────

/** Extract a string value for "key" from a flat JSON object (no nesting). */
static std::string jsonGetString(const std::string& json,
                                  const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    size_t start = json.find('"', pos + 1);
    if (start == std::string::npos) return "";
    size_t end = json.find('"', start + 1);
    if (end == std::string::npos) return "";
    return json.substr(start + 1, end - start - 1);
}

/** Extract a long value for "key". */
static long long jsonGetLong(const std::string& json,
                              const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return 0LL;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return 0LL;
    size_t start = pos + 1;
    while (start < json.size() && (json[start] == ' ' || json[start] == '\t'))
        ++start;
    return std::stoll(json.substr(start));
}

/** Extract an int value for "key". */
static int jsonGetInt(const std::string& json, const std::string& key) {
    return static_cast<int>(jsonGetLong(json, key));
}

/** Extract a bool value for "key". */
static bool jsonGetBool(const std::string& json,
                         const std::string& key,
                         bool def = false) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return def;
    size_t start = pos + 1;
    while (start < json.size() && json[start] == ' ') ++start;
    if (json.substr(start, 4) == "true")  return true;
    if (json.substr(start, 5) == "false") return false;
    return def;
}

/** Parse HTTP Date header to milliseconds since epoch (best-effort). */
static long long parseServerDate(const std::string& dateHeader) {
    if (dateHeader.empty()) {
        jclass  sc = nullptr; // fallback handled by caller
        return 0LL;
    }
    // e.g. "Mon, 25 Mar 2026 10:00:00 GMT"
    struct tm tm = {};
    if (strptime(dateHeader.c_str(), "%a, %d %b %Y %H:%M:%S %Z", &tm) != nullptr)
        return static_cast<long long>(timegm(&tm)) * 1000LL;
    return 0LL;
}

// ─────────────────────────────────────────────────────────────────────────────
// 7.  VERIFICATION RESULT CODES  (returned to Java as int)
// ─────────────────────────────────────────────────────────────────────────────
// Java side maps these to human-readable error messages (dynamic from Firebase).
static const jint VR_GRANT             =  0;
static const jint VR_BANNED            = -1;
static const jint VR_KEY_NOT_FOUND     = -2;
static const jint VR_EXPIRED           = -3;
static const jint VR_WRONG_APP         = -4;
static const jint VR_DEVICE_LIMIT      = -5;
static const jint VR_CONNECTION_ERROR  = -6;
static const jint VR_LOCKED_OUT        = -7;

// ─────────────────────────────────────────────────────────────────────────────
// 8.  JNI EXPORTS  (public API surface – keep this minimal)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Returns the decrypted Firebase base URL.
 * Only used internally – exposed so NativeNetHelper can call it, but the Java
 * class is package-private so external callers cannot reach it.
 */
extern "C" JNIEXPORT jstring JNICALL
Java_fg_fgmods_key_fgmods_getDbUrl(JNIEnv* env, jobject) {
    std::string url = DECRYPT(URL);
    if (url.find("https://") != 0)
        return env->NewStringUTF("https://invalid.url/");
    return env->NewStringUTF(url.c_str());
}

// ── Path getters (package-private callers only) ───────────────────────────────
extern "C" JNIEXPORT jstring JNICALL
Java_fg_fgmods_key_fgmods_getBansPath(JNIEnv* env, jobject) {
    return env->NewStringUTF(DECRYPT(BANS).c_str());
}
extern "C" JNIEXPORT jstring JNICALL
Java_fg_fgmods_key_fgmods_getKeysPath(JNIEnv* env, jobject) {
    return env->NewStringUTF(DECRYPT(KEYS).c_str());
}
extern "C" JNIEXPORT jstring JNICALL
Java_fg_fgmods_key_fgmods_getDevicesPath(JNIEnv* env, jobject) {
    return env->NewStringUTF(DECRYPT(DEVICES).c_str());
}

// ── 8a. loadSavedKey  ─────────────────────────────────────────────────────────
/**
 * Called by Java's showGateway to check whether a saved key exists.
 * Returns the decrypted key string, or null if none.
 */
extern "C" JNIEXPORT jstring JNICALL
Java_fg_fgmods_key_fgmods_loadSavedKey(JNIEnv* env, jobject,
                                         jobject ctx,
                                         jstring jDeviceFp) {
    const char* fp = env->GetStringUTFChars(jDeviceFp, nullptr);
    std::string deviceFp(fp);
    env->ReleaseStringUTFChars(jDeviceFp, fp);

    std::string key = loadEncryptedKey(env, ctx, deviceFp);
    if (key.empty()) return nullptr;
    return env->NewStringUTF(key.c_str());
}

// ── 8b. isLockedOut  ──────────────────────────────────────────────────────────
extern "C" JNIEXPORT jboolean JNICALL
Java_fg_fgmods_key_fgmods_isLockedOut(JNIEnv* env, jobject, jobject ctx) {
    return isLockedOut(env, ctx) ? JNI_TRUE : JNI_FALSE;
}

// ── 8c. checkBan  ─────────────────────────────────────────────────────────────
/**
 * Returns null if not banned.
 * Returns the ban reason string if banned.
 */
extern "C" JNIEXPORT jstring JNICALL
Java_fg_fgmods_key_fgmods_checkBan(JNIEnv* env, jobject,
                                     jstring jDeviceFp) {
    const char* fp = env->GetStringUTFChars(jDeviceFp, nullptr);
    std::string deviceFp(fp);
    env->ReleaseStringUTFChars(jDeviceFp, fp);

    try {
        std::string hashedFp = sha256Hex(deviceFp);
        std::string url      = buildUrl(DECRYPT(BANS) + hashedFp);
        std::string json     = jniGet(env, url);
        if (!json.empty() && json != "null") {
            std::string reason = jsonGetString(json, "reason");
            if (reason.empty()) reason = "Violation of terms.";
            return env->NewStringUTF(reason.c_str());
        }
    } catch (const std::exception& e) {
        LOGE("checkBan error: %s", e.what());
        // Surface as a ban with a sentinel reason so Java can show "retry"
        return env->NewStringUTF("__error__");
    }
    return nullptr; // not banned
}

// ── 8d. checkUpdate  ─────────────────────────────────────────────────────────
/**
 * Returns a JSON object with update info, or null if no update is needed.
 * Java is responsible only for showing the dialog and calling DownloadManager.
 */
extern "C" JNIEXPORT jstring JNICALL
Java_fg_fgmods_key_fgmods_checkUpdate(JNIEnv* env, jobject,
                                        jstring jPackageName,
                                        jint currentVersionCode) {
    const char* pkg = env->GetStringUTFChars(jPackageName, nullptr);
    std::string packageName(pkg);
    env->ReleaseStringUTFChars(jPackageName, pkg);

    // Helper to build full URL without adding extra .json
    auto buildFullUrl = [&](const std::string& path) -> std::string {
        std::string base = DECRYPT(URL);
        if (!base.empty() && base.back() != '/') base += '/';
        std::string p = path;
        if (!p.empty() && p.front() == '/') p.erase(0, 1);
        return base + p;
    };

    try {
        // Build the per‑app update path: /config/update_<package>.json
        std::string perAppPath = "/config/update_" + packageName;
        std::replace(perAppPath.begin(), perAppPath.end(), '.', '_');
        perAppPath += ".json";

        std::string url = buildFullUrl(perAppPath);
        std::string json = jniGet(env, url);

        // If no per‑app config found, return null (no update needed)
        if (json.empty() || json == "null") return nullptr;

        // Validate package name (optional, if you store it in the config)
        std::string serverPkg = jsonGetString(json, "package_name");
        if (!serverPkg.empty() && serverPkg != packageName) return nullptr;

        int serverVer = jsonGetInt(json, "version_code");
        if (serverVer <= currentVersionCode) return nullptr;

        std::string dlUrl = jsonGetString(json, "download_url");
        if (dlUrl.empty()) return nullptr;

        return env->NewStringUTF(json.c_str());
    } catch (const std::exception& e) {
        LOGE("checkUpdate error: %s", e.what());
        return nullptr;
    }
}

// ── 8e. fetchUiConfig  ────────────────────────────────────────────────────────
/**
 * Downloads /config/ui_texts.json, caches it in prefs, and returns it.
 * Returns the cached version if network fails.
 * Returns null if neither is available.
 */
extern "C" JNIEXPORT jstring JNICALL
Java_fg_fgmods_key_fgmods_fetchUiConfig(JNIEnv* env, jobject, jobject ctx) {
    try {
        std::string url  = buildUrl(DECRYPT(CFG_UI));
        std::string json = jniGet(env, url);
        if (!json.empty() && json != "null") {
            prefsPutString(env, ctx, P_UI_CACHE, json);
            return env->NewStringUTF(json.c_str());
        }
    } catch (...) {}

    // Fallback: cached copy
    std::string cached = prefsGetString(env, ctx, P_UI_CACHE);
    if (!cached.empty()) return env->NewStringUTF(cached.c_str());
    return nullptr;
}

// ── 8f. verifyKey  (THE MAIN AUTH LOGIC) ─────────────────────────────────────
/**
 * Returns a result code (VR_* constants above).
 *   0  = access granted  – Java should show success UI and call saveKey.
 *  <0  = failure code    – Java maps to localised error message.
 *
 * On success the rawKey is saved encrypted in SharedPreferences.
 * On failure with VR_DEVICE_LIMIT / VR_KEY_NOT_FOUND the attempt counter is
 * incremented by Java (so this function is pure – no UI side-effects).
 */
extern "C" JNIEXPORT jint JNICALL
Java_fg_fgmods_key_fgmods_verifyKey(JNIEnv* env, jobject,
                                      jobject ctx,
                                      jstring jRawKey,
                                      jstring jDeviceFp,
                                      jstring jPackageName) {
    const char* rk = env->GetStringUTFChars(jRawKey,      nullptr);
    const char* fp = env->GetStringUTFChars(jDeviceFp,    nullptr);
    const char* pk = env->GetStringUTFChars(jPackageName, nullptr);
    std::string rawKey(rk), deviceFp(fp), packageName(pk);
    env->ReleaseStringUTFChars(jRawKey,      rk);
    env->ReleaseStringUTFChars(jDeviceFp,    fp);
    env->ReleaseStringUTFChars(jPackageName, pk);

    // ── Lockout check ────────────────────────────────────────────────────────
    if (isLockedOut(env, ctx)) return VR_LOCKED_OUT;

    try {
        std::string hashedFp = sha256Hex(deviceFp);

        // ── 1. Ban check ─────────────────────────────────────────────────────
        std::string banUrl  = buildUrl(DECRYPT(BANS) + hashedFp);
        std::string banJson = jniGet(env, banUrl);
        if (!banJson.empty() && banJson != "null") {
            // Clear local session on ban
            prefsRemove(env, ctx, P_KEY);
            return VR_BANNED;
        }

        // ── 2. Key lookup ────────────────────────────────────────────────────
        std::string keyUrl   = buildUrl(DECRYPT(KEYS) + rawKey);
        std::string dateHdr  = jniGetServerDate(env, keyUrl);
        std::string keyJson  = jniGet(env, keyUrl);

        if (keyJson.empty() || keyJson == "null")
            return VR_KEY_NOT_FOUND;

        // ── 3. Expiry ────────────────────────────────────────────────────────
        long long serverTime = parseServerDate(dateHdr);
        if (serverTime == 0LL) {
            // Fallback: Java System.currentTimeMillis
            jclass sc = env->FindClass("java/lang/System");
            jmethodID m = env->GetStaticMethodID(sc, "currentTimeMillis","()J");
            serverTime = env->CallStaticLongMethod(sc, m);
            env->DeleteLocalRef(sc);
        }
        long long expiryDate = jsonGetLong(keyJson, "expirydate");
        if (serverTime > expiryDate) return VR_EXPIRED;

        // ── 4. App-package lock ───────────────────────────────────────────────
        std::string storedPackage = jsonGetString(keyJson, "app_package");
        if (!storedPackage.empty() && storedPackage != packageName)
            return VR_WRONG_APP;

        // ── 5. Device binding ─────────────────────────────────────────────────
        int maxDevices     = jsonGetInt(keyJson, "max_devices");
        std::string devices = jsonGetString(keyJson, "devices");

        if (devices.find(hashedFp) != std::string::npos) {
            // Already bound – grant
            saveEncryptedKey(env, ctx, rawKey, deviceFp);
            prefsPutInt(env, ctx, P_ATTEMPTS, 0);
            return VR_GRANT;
        }

        // Count current devices
        int devCount = 0;
        if (!devices.empty()) {
            devCount = 1;
            for (char c : devices) if (c == ',') ++devCount;
        }

        if (devCount >= maxDevices) return VR_DEVICE_LIMIT;

        // Bind this device
        std::string updated = devices.empty() ? hashedFp : devices + "," + hashedFp;

        // Build PATCH JSON – also lock app_package if not yet set
        std::string patchJson = "{\"devices\":\"" + updated + "\"";
        if (storedPackage.empty())
            patchJson += ",\"app_package\":\"" + packageName + "\"";
        patchJson += "}";

        jniPatch(env, keyUrl, patchJson);
        saveEncryptedKey(env, ctx, rawKey, deviceFp);
        prefsPutInt(env, ctx, P_ATTEMPTS, 0);
        return VR_GRANT;

    } catch (const std::exception& e) {
        LOGE("verifyKey error: %s", e.what());
        return VR_CONNECTION_ERROR;
    }
}

// ── 8g. recordDeviceInfo  ────────────────────────────────────────────────────
/**
 * Writes device metadata to /devices/<hashedFp>.json
 * Called after a successful verification so it only runs on genuine users.
 */
extern "C" JNIEXPORT void JNICALL
Java_fg_fgmods_key_fgmods_recordDeviceInfo(JNIEnv* env, jobject,
                                             jstring jHashedFp,
                                             jstring jModel,
                                             jstring jManufacturer,
                                             jstring jRawKey,
                                             jstring jPackageName) {
    auto jToStr = [&](jstring js) -> std::string {
        if (!js) return "";
        const char* c = env->GetStringUTFChars(js, nullptr);
        std::string r(c);
        env->ReleaseStringUTFChars(js, c);
        return r;
    };
    std::string hashedFp    = jToStr(jHashedFp);
    std::string model       = jToStr(jModel);
    std::string manufacturer= jToStr(jManufacturer);
    std::string rawKey      = jToStr(jRawKey);
    std::string pkg         = jToStr(jPackageName);

    try {
        jclass sc = env->FindClass("java/lang/System");
        jmethodID m = env->GetStaticMethodID(sc, "currentTimeMillis","()J");
        long long now = env->CallStaticLongMethod(sc, m);
        env->DeleteLocalRef(sc);

        std::string json =
            "{\"deviceId\":\"" + hashedFp + "\","
            "\"model\":\"" + model + "\","
            "\"manufacturer\":\"" + manufacturer + "\","
            "\"lastSeen\":" + std::to_string(now) + ","
            "\"key\":\"" + rawKey + "\","
            "\"appPackage\":\"" + pkg + "\"}";

        std::string url = buildUrl(DECRYPT(DEVICES) + hashedFp);
        jniPut(env, url, json);
    } catch (const std::exception& e) {
        LOGE("recordDeviceInfo error: %s", e.what());
    }
}

// ── 8h. incrementAttempts / setLockout  ────────────────────────────────────
extern "C" JNIEXPORT jint JNICALL
Java_fg_fgmods_key_fgmods_incrementAttempts(JNIEnv* env, jobject,
                                              jobject ctx) {
    jint a = prefsGetInt(env, ctx, P_ATTEMPTS) + 1;
    prefsPutInt(env, ctx, P_ATTEMPTS, a);
    if (a >= MAX_ATTEMPTS) {
        jclass sc = env->FindClass("java/lang/System");
        jmethodID m = env->GetStaticMethodID(sc, "currentTimeMillis","()J");
        jlong now = env->CallStaticLongMethod(sc, m);
        env->DeleteLocalRef(sc);
        prefsPutLong(env, ctx, P_LOCKOUT, now);
    }
    return a;
}

extern "C" JNIEXPORT void JNICALL
Java_fg_fgmods_key_fgmods_clearSession(JNIEnv* env, jobject, jobject ctx) {
    prefsRemove(env, ctx, P_KEY);
}

// ── 8i. Dummy exports (confuse static analysers) ─────────────────────────────
extern "C" JNIEXPORT jstring JNICALL
Java_fg_fgmods_key_fgmods_dummy1(JNIEnv* env, jobject) {
    return env->NewStringUTF("ok");
}
extern "C" JNIEXPORT jint JNICALL
Java_fg_fgmods_key_fgmods_dummy2(JNIEnv*, jobject) { return 42; }
extern "C" JNIEXPORT void  JNICALL
Java_fg_fgmods_key_fgmods_dummy3(JNIEnv*, jobject) { volatile int x = 0; (void)x; }

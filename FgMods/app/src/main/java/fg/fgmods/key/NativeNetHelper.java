package fg.fgmods.key;

import android.util.Log;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

/**
 * NativeNetHelper
 *
 * Package-private static helper that native-lib.cpp calls back into for HTTP
 * I/O.  All methods now catch and log exceptions instead of throwing them,
 * returning empty strings or simply ignoring errors to avoid JNI pending
 * exceptions.
 *
 * This class is intentionally NOT public so external code cannot call it
 * directly.
 */
/* package-private */ class NativeNetHelper {

    private static final String TAG = "NativeNetHelper";
    private static final int CONNECT_TIMEOUT_MS = 10_000;
    private static final int READ_TIMEOUT_MS    = 15_000;

    /** HTTP GET – returns body as a string, or "" on any error. */
    static String httpGet(String urlStr) {
        HttpURLConnection conn = null;
        try {
            URL url = new URL(urlStr);
            conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("GET");
            conn.setConnectTimeout(CONNECT_TIMEOUT_MS);
            conn.setReadTimeout(READ_TIMEOUT_MS);
            conn.connect();

            try (BufferedReader br = new BufferedReader(
                    new InputStreamReader(conn.getInputStream()))) {
                StringBuilder sb = new StringBuilder();
                String line;
                while ((line = br.readLine()) != null) sb.append(line);
                return sb.toString();
            }
        } catch (Exception e) {
            Log.e(TAG, "httpGet failed: " + urlStr, e);
            return "";
        } finally {
            if (conn != null) conn.disconnect();
        }
    }

    /**
     * HTTP GET – also returns the server "Date" header.
     * Returns empty string on any error.
     */
    static String getServerDate(String urlStr) {
        HttpURLConnection conn = null;
        try {
            URL url = new URL(urlStr);
            conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("GET");
            conn.setConnectTimeout(CONNECT_TIMEOUT_MS);
            conn.setReadTimeout(READ_TIMEOUT_MS);
            conn.connect();
            String date = conn.getHeaderField("Date");
            return date != null ? date : "";
        } catch (Exception e) {
            Log.e(TAG, "getServerDate failed: " + urlStr, e);
            return "";
        } finally {
            if (conn != null) conn.disconnect();
        }
    }

    /** HTTP PATCH (via POST + X-HTTP-Method-Override). */
    static void httpPatch(String urlStr, String jsonBody) {
        HttpURLConnection conn = null;
        try {
            URL url = new URL(urlStr);
            conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("POST");
            conn.setRequestProperty("X-HTTP-Method-Override", "PATCH");
            conn.setRequestProperty("Content-Type", "application/json");
            conn.setConnectTimeout(CONNECT_TIMEOUT_MS);
            conn.setReadTimeout(READ_TIMEOUT_MS);
            conn.setDoOutput(true);

            try (OutputStream os = conn.getOutputStream()) {
                os.write(jsonBody.getBytes(StandardCharsets.UTF_8));
            }
            int resp = conn.getResponseCode();
            if (resp != 200 && resp != 204) {
                Log.w(TAG, "httpPatch returned " + resp + " for " + urlStr);
            }
        } catch (Exception e) {
            Log.e(TAG, "httpPatch failed: " + urlStr, e);
        } finally {
            if (conn != null) conn.disconnect();
        }
    }

    /** HTTP PUT. */
    static void httpPut(String urlStr, String jsonBody) {
        HttpURLConnection conn = null;
        try {
            URL url = new URL(urlStr);
            conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("PUT");
            conn.setRequestProperty("Content-Type", "application/json");
            conn.setConnectTimeout(CONNECT_TIMEOUT_MS);
            conn.setReadTimeout(READ_TIMEOUT_MS);
            conn.setDoOutput(true);

            try (OutputStream os = conn.getOutputStream()) {
                os.write(jsonBody.getBytes(StandardCharsets.UTF_8));
            }
            int resp = conn.getResponseCode();
            if (resp != 200 && resp != 204) {
                Log.w(TAG, "httpPut returned " + resp + " for " + urlStr);
            }
        } catch (Exception e) {
            Log.e(TAG, "httpPut failed: " + urlStr, e);
        } finally {
            if (conn != null) conn.disconnect();
        }
    }
}
package fg.fgmods.key;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.app.DownloadManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewParent;
import android.view.WindowManager;
import android.view.animation.AlphaAnimation;
import android.view.animation.Animation;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.core.content.FileProvider;

import org.json.JSONObject;

import java.io.File;

/**
 * fgmods.java – CLEAN MODERN UI SHELL
 *
 * Backend unchanged. UI: clean dark theme, rounded corners, proper spacing,
 * minimal animations.
 */
public class fgmods {

    static { System.loadLibrary("fgaccess"); }

    native String getDbUrl();
    native String getBansPath();
    native String getKeysPath();
    native String getDevicesPath();

    private native String loadSavedKey(Context ctx, String deviceFp);
    private native boolean isLockedOut(Context ctx);
    private native String checkBan(String deviceFp);
    private native String checkUpdate(String packageName, int currentVersionCode);
    private native String fetchUiConfig(Context ctx);
    private native int verifyKey(Context ctx, String rawKey, String deviceFp, String packageName);
    private native void recordDeviceInfo(String hashedFp, String model, String manufacturer, String rawKey, String packageName);
    private native int incrementAttempts(Context ctx);
    private native void clearSession(Context ctx);

    private static final int VR_GRANT            =  0;
    private static final int VR_BANNED           = -1;
    private static final int VR_KEY_NOT_FOUND    = -2;
    private static final int VR_EXPIRED          = -3;
    private static final int VR_WRONG_APP        = -4;
    private static final int VR_DEVICE_LIMIT     = -5;
    private static final int VR_CONNECTION_ERROR = -6;
    private static final int VR_LOCKED_OUT       = -7;

    private JSONObject uiConfig = null;
    private AlertDialog updateProgressDialog;
    private View overlayView;

    // UI constants – clean, modern dark theme
    private static final int COLOR_BG_DARK    = 0xFF121212;
    private static final int COLOR_SURFACE    = 0xFF1E1E1E;
    private static final int COLOR_ACCENT     = 0xFFBB86FC;   // Material light purple
    private static final int COLOR_PRIMARY    = 0xFF3700B3;   // Darker purple
    private static final int COLOR_TEXT_PRIMARY = 0xFFFFFFFF;
    private static final int COLOR_TEXT_SECONDARY = 0xB3FFFFFF;
    private static final int COLOR_HINT       = 0x80FFFFFF;
    private static final int COLOR_ERROR      = 0xFFCF6679;

    // ─────────────────────────────────────────────────────────────────────────
    // 1.  PUBLIC ENTRY POINT (unchanged)
    // ─────────────────────────────────────────────────────────────────────────
    public void showGateway(final Activity activity) {
        showOverlay(activity);
        activity.getWindow().setFlags(
                WindowManager.LayoutParams.FLAG_SECURE,
                WindowManager.LayoutParams.FLAG_SECURE);

        final String deviceFp = buildDeviceFingerprint(activity);

        checkBannedAndProceed(activity, deviceFp, () ->
            checkForUpdateAndProceed(activity, () -> {
                if (isLockedOut(activity)) {
                    showLockedOutDialog(activity);
                    return;
                }
                String savedKey = loadSavedKey(activity, deviceFp);
                if (savedKey != null) {
                    showAutoLoginDialog(activity, savedKey, deviceFp);
                } else {
                    showLoginDialog(activity, deviceFp);
                }
            })
        );
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 2.  OVERLAY (simple fade)
    // ─────────────────────────────────────────────────────────────────────────
    private void showOverlay(Activity activity) {
        if (overlayView != null) return;
        overlayView = new View(activity);
        overlayView.setBackgroundColor(Color.parseColor("#CC000000"));
        overlayView.setClickable(true);
        overlayView.setFocusable(true);
        overlayView.setFocusableInTouchMode(true);
        overlayView.setOnTouchListener((v, event) -> true);
        overlayView.setLayoutParams(new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        AlphaAnimation fadeIn = new AlphaAnimation(0.0f, 1.0f);
        fadeIn.setDuration(200);
        overlayView.startAnimation(fadeIn);

        ViewGroup decorView = (ViewGroup) activity.getWindow().getDecorView();
        decorView.addView(overlayView);
    }

    private void removeOverlay() {
        if (overlayView == null) return;
        AlphaAnimation fadeOut = new AlphaAnimation(1.0f, 0.0f);
        fadeOut.setDuration(200);
        fadeOut.setAnimationListener(new Animation.AnimationListener() {
            @Override public void onAnimationStart(Animation animation) {}
            @Override public void onAnimationEnd(Animation animation) {
                ViewParent parent = overlayView.getParent();
                if (parent instanceof ViewGroup)
                    ((ViewGroup) parent).removeView(overlayView);
                overlayView = null;
            }
            @Override public void onAnimationRepeat(Animation animation) {}
        });
        overlayView.startAnimation(fadeOut);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 3.  DEVICE FINGERPRINT (unchanged)
    // ─────────────────────────────────────────────────────────────────────────
    private String buildDeviceFingerprint(Context ctx) {
        String id = Settings.Secure.getString(ctx.getContentResolver(), Settings.Secure.ANDROID_ID);
        return (id == null || id.isEmpty()) ? "unknown_id" : id;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 4.  BAN CHECK (unchanged logic, UI uses clean dialogs)
    // ─────────────────────────────────────────────────────────────────────────
    private void checkBannedAndProceed(final Activity activity,
                                       final String deviceFp,
                                       final Runnable onNotBanned) {
        Dialog loading = createLoadingDialog(activity);
        loading.show();

        new Thread(() -> {
            String reason = checkBan(deviceFp);
            new Handler(Looper.getMainLooper()).post(() -> {
                loading.dismiss();
                if (reason == null) {
                    if (onNotBanned != null) onNotBanned.run();
                } else if ("__error__".equals(reason)) {
                    showNetworkErrorDialog(activity, deviceFp, onNotBanned);
                } else {
                    showBannedDialog(activity, reason);
                }
            });
        }).start();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 5.  OTA UPDATE CHECK (unchanged logic)
    // ─────────────────────────────────────────────────────────────────────────
    private void checkForUpdateAndProceed(final Activity activity,
                                          final Runnable onComplete) {
        new Thread(() -> {
            try {
                String packageName = activity.getPackageName();
                int versionCode = activity.getPackageManager()
                        .getPackageInfo(packageName, 0).versionCode;
                String updateJson = checkUpdate(packageName, versionCode);

                new Handler(Looper.getMainLooper()).post(() -> {
                    if (updateJson == null) {
                        if (onComplete != null) onComplete.run();
                        return;
                    }
                    try {
                        JSONObject data = new JSONObject(updateJson);
                        String dlUrl   = data.optString("download_url", "");
                        String msg     = data.optString("update_message",
                                "A new version is available.");
                        boolean force  = data.optBoolean("force_update", true);
                        showUpdateDialog(activity, msg, dlUrl, force);
                    } catch (Exception e) {
                        if (onComplete != null) onComplete.run();
                    }
                });
            } catch (Exception e) {
                new Handler(Looper.getMainLooper()).post(() -> {
                    if (onComplete != null) onComplete.run();
                });
            }
        }).start();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 6.  UI CONFIG (unchanged)
    // ─────────────────────────────────────────────────────────────────────────
    private void loadUiConfig(final Context ctx, final Runnable onComplete) {
        new Thread(() -> {
            String json = fetchUiConfig(ctx);
            if (json != null) {
                try { uiConfig = new JSONObject(json); } catch (Exception ignored) {}
            }
            if (onComplete != null) onComplete.run();
        }).start();
    }

    private String getUiString(String key, String def) {
        if (uiConfig != null && uiConfig.has(key))
            return uiConfig.optString(key, def);
        return def;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 7.  CLEAN UI DIALOGS (simple, modern)
    // ─────────────────────────────────────────────────────────────────────────
    private Dialog createLoadingDialog(Activity activity) {
        AlertDialog.Builder builder = new AlertDialog.Builder(activity);
        builder.setCancelable(false);
        LinearLayout layout = createCardLayout(activity);
        layout.setGravity(Gravity.CENTER);
        layout.setPadding(64, 64, 64, 64);

        ProgressBar progressBar = new ProgressBar(activity);
        progressBar.setIndeterminate(true);
        progressBar.setIndeterminateTintList(android.content.res.ColorStateList.valueOf(COLOR_ACCENT));

        TextView text = new TextView(activity);
        text.setText(getUiString("loading_text", "Loading..."));
        text.setTextColor(COLOR_TEXT_PRIMARY);
        text.setTextSize(16);
        text.setPadding(0, 32, 0, 0);
        text.setGravity(Gravity.CENTER);

        layout.addView(progressBar);
        layout.addView(text);
        builder.setView(layout);
        Dialog dialog = builder.create();
        dialog.getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        return dialog;
    }

    private void showLoginDialog(final Activity activity, final String deviceFp) {
        Dialog loading = createLoadingDialog(activity);
        loading.show();

        loadUiConfig(activity, () -> {
            loading.dismiss();
            activity.runOnUiThread(() -> {
                AlertDialog.Builder builder = new AlertDialog.Builder(activity);
                builder.setCancelable(false);
                LinearLayout root = createCardLayout(activity);
                root.setPadding(48, 48, 48, 48);
                root.setOrientation(LinearLayout.VERTICAL);

                // Title
                TextView title = new TextView(activity);
                title.setText(getUiString("title", "🔐 VAULT ACCESS"));
                title.setTextColor(COLOR_TEXT_PRIMARY);
                title.setTextSize(20);
                title.setTypeface(Typeface.DEFAULT_BOLD);
                title.setGravity(Gravity.CENTER);
                root.addView(title);

                // Subtitle
                TextView subtitle = new TextView(activity);
                subtitle.setText(getUiString("subtitle", "Enter license key to unlock features"));
                subtitle.setTextColor(COLOR_TEXT_SECONDARY);
                subtitle.setTextSize(13);
                subtitle.setGravity(Gravity.CENTER);
                subtitle.setPadding(0, 12, 0, 32);
                root.addView(subtitle);

                // Input field
                final EditText keyInput = new EditText(activity);
                keyInput.setHint(getUiString("key_hint", "fgmods_XXXX-XXXX-XXXX"));
                keyInput.setHintTextColor(COLOR_HINT);
                keyInput.setTextColor(COLOR_TEXT_PRIMARY);
                keyInput.setSingleLine(true);
                keyInput.setBackground(createInputBackground());
                keyInput.setPadding(24, 20, 24, 20);
                root.addView(keyInput);

                // Device ID (monospaced)
                TextView deviceId = new TextView(activity);
                deviceId.setText("ID: " + deviceFp);
                deviceId.setTextColor(COLOR_HINT);
                deviceId.setTextSize(10);
                deviceId.setTypeface(Typeface.MONOSPACE);
                deviceId.setPadding(0, 16, 0, 24);
                root.addView(deviceId);

                // Verify button
                Button unlockBtn = new Button(activity);
                unlockBtn.setText(getUiString("verify_button", "VERIFY LICENSE"));
                unlockBtn.setBackground(createButtonBackground(COLOR_ACCENT, 24));
                unlockBtn.setTextColor(Color.BLACK);
                unlockBtn.setTypeface(Typeface.DEFAULT_BOLD);
                unlockBtn.setPadding(0, 16, 0, 16);
                root.addView(unlockBtn);

                // Contact button
                Button adminBtn = new Button(activity);
                adminBtn.setText(getUiString("contact_button", "CONTACT ADMIN"));
                adminBtn.setBackground(createOutlineBackground(COLOR_ACCENT, 24));
                adminBtn.setTextColor(COLOR_ACCENT);
                adminBtn.setTypeface(Typeface.DEFAULT_BOLD);
                adminBtn.setPadding(0, 16, 0, 16);
                // Create LayoutParams for the admin button
                LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,  // width
                    LinearLayout.LayoutParams.WRAP_CONTENT   // height
                );
                
                // Convert dp to pixels (16dp is a typical margin)
                int marginInDp = 16;
                float density = activity.getResources().getDisplayMetrics().density;
                int marginInPx = (int) (marginInDp * density);
                
                // Set top margin (space above the button)
                params.topMargin = marginInPx;
                
                // Apply the params to the button
                adminBtn.setLayoutParams(params);
                root.addView(adminBtn);

                final ProgressBar spinner = new ProgressBar(activity);
                spinner.setVisibility(View.GONE);
                root.addView(spinner);

                builder.setView(root);
                final AlertDialog dialog = builder.create();
                dialog.getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
                dialog.show();

                unlockBtn.setOnClickListener(v -> {
                    String rawKey = keyInput.getText().toString().trim();
                    if (rawKey.isEmpty()) {
                        Toast.makeText(activity, getUiString("empty_key_error", "Key cannot be empty"), Toast.LENGTH_SHORT).show();
                        return;
                    }
                    unlockBtn.setVisibility(View.GONE);
                    adminBtn.setVisibility(View.GONE);
                    spinner.setVisibility(View.VISIBLE);
                    runVerification(activity, dialog, rawKey, deviceFp, false, unlockBtn, spinner);
                });

                adminBtn.setOnClickListener(v ->
                        activity.startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse("https://t.me/furjack"))));
            });
        });
    }

    private void showAutoLoginDialog(final Activity activity,
                                     final String savedKey,
                                     final String deviceFp) {
        loadUiConfig(activity, () ->
            activity.runOnUiThread(() -> {
                AlertDialog.Builder builder = new AlertDialog.Builder(activity);
                builder.setCancelable(false);
                LinearLayout root = createCardLayout(activity);
                root.setPadding(48, 48, 48, 48);
                root.setGravity(Gravity.CENTER);

                ProgressBar progressBar = new ProgressBar(activity);
                progressBar.setIndeterminate(true);
                progressBar.setIndeterminateTintList(android.content.res.ColorStateList.valueOf(COLOR_ACCENT));

                TextView text = new TextView(activity);
                text.setText(getUiString("auto_title", "Verifying saved session..."));
                text.setTextColor(COLOR_TEXT_PRIMARY);
                text.setTextSize(16);
                text.setPadding(0, 32, 0, 0);

                root.addView(progressBar);
                root.addView(text);
                builder.setView(root);
                final AlertDialog dialog = builder.create();
                dialog.getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
                dialog.show();
                runVerification(activity, dialog, savedKey, deviceFp, true, null, null);
            })
        );
    }

    private void showLockedOutDialog(Activity activity) {
        loadUiConfig(activity, () ->
            activity.runOnUiThread(() -> {
                AlertDialog.Builder builder = new AlertDialog.Builder(activity);
                builder.setCancelable(false);
                LinearLayout root = createCardLayout(activity);
                root.setPadding(48, 48, 48, 48);
                root.setGravity(Gravity.CENTER);

                TextView title = new TextView(activity);
                title.setText(getUiString("locked_title", "Too Many Attempts"));
                title.setTextColor(COLOR_ERROR);
                title.setTextSize(18);
                title.setTypeface(Typeface.DEFAULT_BOLD);
                title.setGravity(Gravity.CENTER);
                root.addView(title);

                TextView msg = new TextView(activity);
                msg.setText(getUiString("locked_subtitle", "Locked for 15 minutes."));
                msg.setTextColor(COLOR_TEXT_SECONDARY);
                msg.setTextSize(14);
                msg.setGravity(Gravity.CENTER);
                msg.setPadding(0, 16, 0, 0);
                root.addView(msg);

                builder.setView(root);
                AlertDialog dialog = builder.create();
                dialog.getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
                dialog.show();
                removeOverlay();
            })
        );
    }

    private void showBannedDialog(Activity activity, String reason) {
        loadUiConfig(activity, () ->
            activity.runOnUiThread(() -> {
                AlertDialog.Builder builder = new AlertDialog.Builder(activity);
                builder.setCancelable(false);
                LinearLayout root = createCardLayout(activity);
                root.setPadding(48, 48, 48, 48);
                root.setOrientation(LinearLayout.VERTICAL);

                TextView title = new TextView(activity);
                title.setText(getUiString("ban_title", "Device Banned"));
                title.setTextColor(COLOR_ERROR);
                title.setTextSize(18);
                title.setTypeface(Typeface.DEFAULT_BOLD);
                title.setGravity(Gravity.CENTER);
                root.addView(title);

                TextView msg = new TextView(activity);
                msg.setText(String.format(getUiString("ban_subtitle", "Reason: %s"), reason));
                msg.setTextColor(COLOR_TEXT_SECONDARY);
                msg.setTextSize(14);
                msg.setGravity(Gravity.CENTER);
                msg.setPadding(0, 16, 0, 24);
                root.addView(msg);

                Button appealBtn = new Button(activity);
                appealBtn.setText(getUiString("appeal_button", "APPEAL BAN (TELEGRAM)"));
                appealBtn.setBackground(createButtonBackground(COLOR_ACCENT, 24));
                appealBtn.setTextColor(Color.BLACK);
                appealBtn.setTypeface(Typeface.DEFAULT_BOLD);
                appealBtn.setOnClickListener(v ->
                        activity.startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse("https://t.me/furjack"))));
                root.addView(appealBtn);

                builder.setView(root);
                AlertDialog dialog = builder.create();
                dialog.getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
                dialog.show();
                removeOverlay();
            })
        );
    }

    private void showUpdateDialog(Activity activity, String message, String url, boolean force) {
        AlertDialog.Builder builder = new AlertDialog.Builder(activity);
        builder.setCancelable(false);
        LinearLayout root = createCardLayout(activity);
        root.setPadding(48, 48, 48, 48);
        root.setOrientation(LinearLayout.VERTICAL);

        TextView title = new TextView(activity);
        title.setText(getUiString("update_title", "Update Available"));
        title.setTextColor(COLOR_TEXT_PRIMARY);
        title.setTextSize(18);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setGravity(Gravity.CENTER);
        root.addView(title);

        TextView msg = new TextView(activity);
        msg.setText(message);
        msg.setTextColor(COLOR_TEXT_SECONDARY);
        msg.setTextSize(14);
        msg.setGravity(Gravity.CENTER);
        msg.setPadding(0, 16, 0, 32);
        root.addView(msg);

        Button updateBtn = new Button(activity);
        updateBtn.setText(getUiString("update_now", "Update Now"));
        updateBtn.setBackground(createButtonBackground(COLOR_ACCENT, 24));
        updateBtn.setTextColor(Color.BLACK);
        updateBtn.setTypeface(Typeface.DEFAULT_BOLD);
        updateBtn.setOnClickListener(v -> startUpdateDownload(activity, url));
        root.addView(updateBtn);

        builder.setView(root);
        AlertDialog dialog = builder.create();
        dialog.getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        dialog.show();
    }

    private void startUpdateDownload(final Activity activity, final String url) {
        // Simplified download progress dialog
        AlertDialog.Builder builder = new AlertDialog.Builder(activity);
        builder.setCancelable(false);
        LinearLayout root = createCardLayout(activity);
        root.setPadding(48, 48, 48, 48);
        root.setGravity(Gravity.CENTER);

        ProgressBar progressBar = new ProgressBar(activity);
        progressBar.setIndeterminate(true);
        progressBar.setIndeterminateTintList(android.content.res.ColorStateList.valueOf(COLOR_ACCENT));

        TextView text = new TextView(activity);
        text.setText(getUiString("update_download_progress", "Downloading update..."));
        text.setTextColor(COLOR_TEXT_PRIMARY);
        text.setTextSize(16);
        text.setPadding(0, 32, 0, 0);

        root.addView(progressBar);
        root.addView(text);
        builder.setView(root);
        updateProgressDialog = builder.create();
        updateProgressDialog.getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        updateProgressDialog.show();

        // Permission and download logic (unchanged from original)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O &&
                !activity.getPackageManager().canRequestPackageInstalls()) {
            updateProgressDialog.dismiss();
            new AlertDialog.Builder(activity)
                    .setTitle("Permission Required")
                    .setMessage("Please allow installation from unknown sources.")
                    .setCancelable(false)
                    .setPositiveButton("Settings", (d, w) ->
                            activity.startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                                    Uri.parse("package:" + activity.getPackageName()))))
                    .setNegativeButton("Cancel", (d, w) ->
                            showUpdateDialog(activity,
                                    getUiString("update_error_permission", "Permission required."), url, true))
                    .show();
            return;
        }

        DownloadManager.Request req = new DownloadManager.Request(Uri.parse(url));
        req.setTitle(getUiString("update_download_title", "Downloading update..."));
        req.setDescription(getUiString("update_download_desc", "Please wait..."));
        req.setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED);
        req.setDestinationInExternalFilesDir(activity, null, "app_update.apk");

        DownloadManager dm = (DownloadManager) activity.getSystemService(Context.DOWNLOAD_SERVICE);
        final long dlId = dm.enqueue(req);

        BroadcastReceiver receiver = new BroadcastReceiver() {
            @Override public void onReceive(Context ctx, Intent intent) {
                long id = intent.getLongExtra(DownloadManager.EXTRA_DOWNLOAD_ID, -1);
                if (id != dlId) return;
                DownloadManager.Query q = new DownloadManager.Query();
                q.setFilterById(id);
                Cursor cursor = dm.query(q);
                if (cursor.moveToFirst()) {
                    int status = cursor.getInt(cursor.getColumnIndex(DownloadManager.COLUMN_STATUS));
                    if (updateProgressDialog != null && updateProgressDialog.isShowing())
                        updateProgressDialog.dismiss();
                    if (status == DownloadManager.STATUS_SUCCESSFUL) {
                        String localUri = cursor.getString(cursor.getColumnIndex(DownloadManager.COLUMN_LOCAL_URI));
                        File apk = new File(Uri.parse(localUri).getPath());
                        Uri apkUri = FileProvider.getUriForFile(activity,
                                activity.getPackageName() + ".fileprovider", apk);
                        Intent install = new Intent(Intent.ACTION_VIEW);
                        install.setDataAndType(apkUri, "application/vnd.android.package-archive");
                        install.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                        activity.startActivity(install);
                    } else {
                        Toast.makeText(activity,
                                getUiString("update_download_failed", "Download failed. Please try again."),
                                Toast.LENGTH_LONG).show();
                        showUpdateDialog(activity,
                                getUiString("update_retry", "Update failed. Please try again."),
                                url, true);
                    }
                }
                cursor.close();
                try { activity.unregisterReceiver(this); }
                catch (IllegalArgumentException ignored) {}
            }
        };

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
            activity.registerReceiver(receiver, new IntentFilter(DownloadManager.ACTION_DOWNLOAD_COMPLETE), Context.RECEIVER_EXPORTED);
        else
            activity.registerReceiver(receiver, new IntentFilter(DownloadManager.ACTION_DOWNLOAD_COMPLETE));
    }

    private void showNetworkErrorDialog(final Activity activity, final String deviceFp, final Runnable onRetry) {
        AlertDialog.Builder builder = new AlertDialog.Builder(activity);
        builder.setCancelable(false);
        LinearLayout root = createCardLayout(activity);
        root.setPadding(48, 48, 48, 48);
        root.setOrientation(LinearLayout.VERTICAL);

        TextView title = new TextView(activity);
        title.setText(getUiString("error_title", "Connection Error"));
        title.setTextColor(COLOR_ERROR);
        title.setTextSize(18);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setGravity(Gravity.CENTER);
        root.addView(title);

        TextView msg = new TextView(activity);
        msg.setText(getUiString("network_error", "Failed to verify device status. Please check your internet connection."));
        msg.setTextColor(COLOR_TEXT_SECONDARY);
        msg.setTextSize(14);
        msg.setGravity(Gravity.CENTER);
        msg.setPadding(0, 16, 0, 32);
        root.addView(msg);

        Button retryBtn = new Button(activity);
        retryBtn.setText(getUiString("retry", "Retry"));
        retryBtn.setBackground(createButtonBackground(COLOR_ACCENT, 24));
        retryBtn.setTextColor(Color.BLACK);
        retryBtn.setOnClickListener(v -> checkBannedAndProceed(activity, deviceFp, onRetry));
        root.addView(retryBtn);

        Button exitBtn = new Button(activity);
        exitBtn.setText(getUiString("exit", "Exit"));
        exitBtn.setBackground(createOutlineBackground(COLOR_ACCENT, 24));
        exitBtn.setTextColor(COLOR_ACCENT);
        exitBtn.setOnClickListener(v -> {
            activity.finish();
            removeOverlay();
        });
        root.addView(exitBtn);

        builder.setView(root);
        AlertDialog dialog = builder.create();
        dialog.getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        dialog.show();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 8.  VERIFICATION (unchanged)
    // ─────────────────────────────────────────────────────────────────────────
    private void runVerification(final Activity activity,
                                 final AlertDialog dialog,
                                 final String rawKey,
                                 final String deviceFp,
                                 final boolean isAuto,
                                 final Button btn,
                                 final ProgressBar spinner) {
        final String packageName = activity.getPackageName();

        new Thread(() -> {
            int result = verifyKey(activity, rawKey, deviceFp, packageName);

            new Handler(Looper.getMainLooper()).post(() -> {
                if (result == VR_GRANT) {
                    dialog.dismiss();
                    Toast.makeText(activity,
                            getUiString("grant_toast", "Access Granted"),
                            Toast.LENGTH_SHORT).show();
                    removeOverlay();
                    new Thread(() -> recordDeviceInfo(
                            sha256Hex(deviceFp),
                            Build.MODEL,
                            Build.MANUFACTURER,
                            rawKey,
                            packageName)).start();
                    return;
                }

                if (isAuto) {
                    clearSession(activity);
                    dialog.dismiss();
                    showLoginDialog(activity, deviceFp);
                    return;
                }

                if (result == VR_LOCKED_OUT) {
                    dialog.dismiss();
                    showLockedOutDialog(activity);
                    return;
                }

                if (result == VR_BANNED) {
                    clearSession(activity);
                    dialog.dismiss();
                    showBannedDialog(activity, getUiString("banned_reason_generic", "Violation of terms."));
                    return;
                }

                String msg = codeToMessage(result);
                int attempts = incrementAttempts(activity);
                if (attempts >= 10) {
                    dialog.dismiss();
                    showLockedOutDialog(activity);
                } else {
                    Toast.makeText(activity,
                            msg + " (" + (10 - attempts) + " left)",
                            Toast.LENGTH_LONG).show();
                    if (btn != null) btn.setVisibility(View.VISIBLE);
                    if (spinner != null) spinner.setVisibility(View.GONE);
                }
            });
        }).start();
    }

    private String codeToMessage(int code) {
        switch (code) {
            case VR_KEY_NOT_FOUND:    return getUiString("error_key_not_found",  "License key not found.");
            case VR_EXPIRED:          return getUiString("error_expired",         "License has expired.");
            case VR_WRONG_APP:        return getUiString("error_wrong_app",       "This key is not valid for this app.");
            case VR_DEVICE_LIMIT:     return getUiString("error_device_limit",    "Device limit reached for this key.");
            case VR_CONNECTION_ERROR: return getUiString("error_connection",      "Database connection failed.");
            default:                  return getUiString("error_generic",         "Verification failed.");
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 9.  SHA-256 (unchanged)
    // ─────────────────────────────────────────────────────────────────────────
    private String sha256Hex(String input) {
        try {
            java.security.MessageDigest md = java.security.MessageDigest.getInstance("SHA-256");
            byte[] hash = md.digest(input.getBytes(java.nio.charset.StandardCharsets.UTF_8));
            StringBuilder sb = new StringBuilder();
            for (byte b : hash) sb.append(String.format("%02x", b));
            return sb.toString();
        } catch (Exception e) { return input; }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 10.  UI UTILITIES (simple, clean)
    // ─────────────────────────────────────────────────────────────────────────
    private LinearLayout createCardLayout(Context ctx) {
        LinearLayout layout = new LinearLayout(ctx);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setBackground(createCardBackground());
        layout.setElevation(8);
        return layout;
    }

    private Drawable createCardBackground() {
        GradientDrawable gd = new GradientDrawable();
        gd.setColor(COLOR_SURFACE);
        gd.setCornerRadius(24);
        return gd;
    }

    private Drawable createInputBackground() {
        GradientDrawable gd = new GradientDrawable();
        gd.setColor(COLOR_BG_DARK);
        gd.setCornerRadius(16);
        gd.setStroke(1, COLOR_HINT);
        return gd;
    }

    private Drawable createButtonBackground(int color, int radius) {
        GradientDrawable gd = new GradientDrawable();
        gd.setColor(color);
        gd.setCornerRadius(radius);
        return gd;
    }

    private Drawable createOutlineBackground(int color, int radius) {
        GradientDrawable gd = new GradientDrawable();
        gd.setColor(Color.TRANSPARENT);
        gd.setStroke(2, color);
        gd.setCornerRadius(radius);
        return gd;
    }
}
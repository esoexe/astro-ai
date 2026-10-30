package io.github.esoexe.astroai;

import android.Manifest;
import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.MediaStore;
import android.speech.RecognitionListener;
import android.speech.RecognizerIntent;
import android.speech.SpeechRecognizer;
import android.speech.tts.TextToSpeech;
import android.speech.tts.UtteranceProgressListener;
import android.speech.tts.Voice;
import android.util.Base64;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.webkit.CookieManager;
import android.webkit.JavascriptInterface;
import android.webkit.PermissionRequest;
import android.webkit.RenderProcessGoneDetail;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.net.HttpURLConnection;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * A.S.T.R.O AI for Android.
 *
 * The same C++ engine as the desktop app runs on the phone (libastro_engine.so, started from nativeLibraryDir)
 * and listens on 127.0.0.1 only. Every launch uses a new random port and a new random token; the token is given
 * only to this WebView (as its session cookie), so other apps on the phone cannot use the engine.
 *
 * The page talks to Android through the "AstroAndroid" bridge (web/android.js): speech, saving files, links.
 */
public class MainActivity extends Activity {
    private static final int REQ_FILE = 11;
    private static final int REQ_MIC = 12;

    private final Handler ui = new Handler(Looper.getMainLooper());
    private final SecureRandom rng = new SecureRandom();

    private FrameLayout root;
    private WebView web;
    private LinearLayout splash;
    private TextView splashText;
    private TextView splashLog;
    private ProgressBar splashSpin;
    private Button splashButton;

    private volatile Process engine;
    private volatile int port;
    private volatile String token;
    private volatile boolean stopping;
    private volatile String pageNonce;
    private volatile boolean nonceClaimed;
    private boolean pageReady;
    private String pendingShare;

    private ValueCallback<Uri[]> fileCallback;
    private PermissionRequest pendingWebPermission;
    private Runnable pendingMic;

    private TextToSpeech tts;
    private volatile boolean ttsReady;
    private SpeechRecognizer stt;
    private String sttId;

    private Object backCallback;

    // ---------------------------------------------------------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------------------------------------------------------
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        edgeToEdge();
        buildViews();
        setContentView(root);
        setupWebView();
        setupBack();
        tts = new TextToSpeech(getApplicationContext(), status -> {
            ttsReady = status == TextToSpeech.SUCCESS;
            if (ttsReady) {
                tts.setOnUtteranceProgressListener(new TtsListener());
                js("window.__astroAndroidVoices && window.__astroAndroidVoices()");
            }
        });
        handleIntent(getIntent());
        startEngine();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        handleIntent(intent);
    }

    @Override
    protected void onResume() {
        super.onResume();
        Process p = engine;
        if (!stopping && p != null && !p.isAlive() && splash.getVisibility() != View.VISIBLE) startEngine();
    }

    @Override
    protected void onDestroy() {
        if (isFinishing()) stopEngine();
        if (tts != null) tts.shutdown();
        if (stt != null) stt.destroy();
        if (web != null) {
            root.removeView(web);
            web.destroy();
        }
        super.onDestroy();
    }

    @Override
    public void onConfigurationChanged(Configuration c) {
        super.onConfigurationChanged(c);
        root.requestApplyInsets();
    }

    // ---------------------------------------------------------------------------------------------------------
    // Views
    // ---------------------------------------------------------------------------------------------------------
    private boolean nightMode() {
        return (getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES;
    }

    private void edgeToEdge() {
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }

    private void buildViews() {
        root = new FrameLayout(this);
        root.setBackgroundColor(nightMode() ? 0xFF0A0A0C : 0xFFFBFBFA);
        // Keep the app clear of the status bar, navigation bar, camera cutout and keyboard.
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                Insets i = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.ime() | WindowInsets.Type.displayCutout());
                v.setPadding(i.left, i.top, i.right, i.bottom);
                return WindowInsets.CONSUMED;
            }
            v.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                    insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            return insets.consumeSystemWindowInsets();
        });

        web = new WebView(this);
        web.setBackgroundColor(Color.TRANSPARENT);
        root.addView(web, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        splash = new LinearLayout(this);
        splash.setOrientation(LinearLayout.VERTICAL);
        splash.setGravity(Gravity.CENTER);
        splash.setBackgroundColor(nightMode() ? 0xFF0A0A0C : 0xFFFBFBFA);
        int pad = dp(28);
        splash.setPadding(pad, pad, pad, pad);
        ImageView logo = new ImageView(this);
        logo.setImageResource(R.mipmap.ic_launcher);
        logo.setContentDescription(null);
        splash.addView(logo, new LinearLayout.LayoutParams(dp(88), dp(88)));
        splashText = new TextView(this);
        splashText.setTextSize(17);
        splashText.setTextColor(nightMode() ? 0xFFF3F4F3 : 0xFF16181A);
        splashText.setGravity(Gravity.CENTER);
        splashText.setPadding(0, dp(18), 0, dp(12));
        splash.addView(splashText);
        splashSpin = new ProgressBar(this);
        splash.addView(splashSpin, new LinearLayout.LayoutParams(dp(32), dp(32)));
        splashButton = new Button(this);
        splashButton.setText(R.string.restart);
        splashButton.setOnClickListener(v -> startEngine());
        splash.addView(splashButton);
        ScrollView logScroll = new ScrollView(this);
        splashLog = new TextView(this);
        splashLog.setTypeface(Typeface.MONOSPACE);
        splashLog.setTextSize(11);
        splashLog.setTextColor(nightMode() ? 0xFFA9B0AD : 0xFF4D5552);
        splashLog.setTextIsSelectable(true);
        logScroll.addView(splashLog);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f);
        lp.topMargin = dp(16);
        splash.addView(logScroll, lp);
        root.addView(splash, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    private void showSplash(String text, boolean failed, String log) {
        splash.setVisibility(View.VISIBLE);
        splashText.setText(text);
        splashSpin.setVisibility(failed ? View.GONE : View.VISIBLE);
        splashButton.setVisibility(failed ? View.VISIBLE : View.GONE);
        splashLog.setText(log == null ? "" : log);
        ((View) splashLog.getParent()).setVisibility(failed && log != null && !log.isEmpty() ? View.VISIBLE : View.GONE);
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    // ---------------------------------------------------------------------------------------------------------
    // WebView
    // ---------------------------------------------------------------------------------------------------------
    private boolean isOurs(Uri u) {
        return u != null && "http".equals(u.getScheme()) && "127.0.0.1".equals(u.getHost()) && u.getPort() == port;
    }

    private void setupWebView() {
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setAllowFileAccess(false);
        s.setAllowContentAccess(false);
        s.setSupportMultipleWindows(false);
        s.setJavaScriptCanOpenWindowsAutomatically(false);
        s.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        s.setUserAgentString(s.getUserAgentString() + " AstroAI-Android/" + versionName());
        WebView.setWebContentsDebuggingEnabled((getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0);
        web.addJavascriptInterface(new Bridge(), "AstroAndroid");

        web.setWebViewClient(new WebViewClient() {
            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest req) {
                if (isOurs(req.getUrl())) return false;
                openExternal(req.getUrl().toString());
                return true;
            }

            @Override
            public void onPageStarted(WebView view, String url, android.graphics.Bitmap favicon) {
                // A new page gets a new one-time bridge secret (see Bridge.claim).
                pageNonce = randomHex(16);
                nonceClaimed = false;
                pageReady = false;
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                pageReady = true;
                splash.setVisibility(View.GONE);
                if (ttsReady) js("window.__astroAndroidVoices && window.__astroAndroidVoices()");
                deliverShare();
            }

            @Override
            public boolean onRenderProcessGone(WebView view, RenderProcessGoneDetail detail) {
                // The page's renderer crashed or was reclaimed: rebuild the activity (the engine keeps running).
                recreate();
                return true;
            }
        });

        web.setWebChromeClient(new WebChromeClient() {
            @Override
            public void onPermissionRequest(PermissionRequest request) {
                ui.post(() -> {
                    boolean audioOnly = true;
                    for (String r : request.getResources()) if (!PermissionRequest.RESOURCE_AUDIO_CAPTURE.equals(r)) audioOnly = false;
                    if (!audioOnly || !isOurs(request.getOrigin())) { request.deny(); return; }
                    if (hasMic()) request.grant(new String[]{PermissionRequest.RESOURCE_AUDIO_CAPTURE});
                    else { pendingWebPermission = request; requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, REQ_MIC); }
                });
            }

            @Override
            public boolean onShowFileChooser(WebView view, ValueCallback<Uri[]> callback, FileChooserParams params) {
                if (fileCallback != null) fileCallback.onReceiveValue(null);
                fileCallback = callback;
                Intent intent = params.createIntent();
                if (params.getMode() == FileChooserParams.MODE_OPEN_MULTIPLE) intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
                try {
                    startActivityForResult(intent, REQ_FILE);
                } catch (ActivityNotFoundException e) {
                    fileCallback = null;
                    return false;
                }
                return true;
            }
        });

        web.setDownloadListener((url, userAgent, disposition, mime, length) -> {
            Uri u = Uri.parse(url);
            if (!isOurs(u)) openExternal(url);
        });
    }

    private void setupBack() {
        if (Build.VERSION.SDK_INT >= 33) {
            OnBackInvokedCallback cb = this::goBack;
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT, cb);
            backCallback = cb;
        }
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        goBack();
    }

    /** Back closes an open preview first, then goes back in the app, then leaves (the engine keeps running). */
    private void goBack() {
        if (web == null || splash.getVisibility() == View.VISIBLE) { moveTaskToBack(true); return; }
        web.evaluateJavascript("(window.__astroAndroidBack && window.__astroAndroidBack()) ? 'y' : 'n'", res -> {
            if (res != null && res.contains("y")) return;
            if (web.canGoBack()) web.goBack();
            else moveTaskToBack(true);
        });
    }

    private void js(String code) {
        ui.post(() -> { if (web != null) web.evaluateJavascript(code, null); });
    }

    private void emit(JSONObject e) {
        js("window.__astroAndroidEvent && window.__astroAndroidEvent(" + e + ")");
    }

    // ---------------------------------------------------------------------------------------------------------
    // Engine
    // ---------------------------------------------------------------------------------------------------------
    private void startEngine() {
        stopping = false;
        showSplash(getString(R.string.starting), false, null);
        new Thread(() -> {
            try {
                File home = new File(getFilesDir(), "app");
                preparePayload(home);
                File data = new File(getFilesDir(), "data");
                File tmp = new File(getCacheDir(), "tmp");
                //noinspection ResultOfMethodCallIgnored
                data.mkdirs();
                //noinspection ResultOfMethodCallIgnored
                tmp.mkdirs();
                stopEngineNow();
                port = freePort();
                token = randomHex(32);

                File exe = new File(getApplicationInfo().nativeLibraryDir, "libastro_engine.so");
                if (!exe.exists()) throw new IOException("The engine is missing from this APK (" + exe + ")");
                ProcessBuilder pb = new ProcessBuilder(exe.getAbsolutePath());
                pb.directory(home);
                Map<String, String> env = pb.environment();
                env.put("PORT", String.valueOf(port));
                env.put("ASTRO_BIND", "127.0.0.1");
                env.put("ASTRO_AUTH", "off");                 // no password screen...
                env.put("ASTRO_APP_TOKEN", token);            // ...but only this app's WebView gets in
                env.put("ASTRO_DATA_DIR", data.getAbsolutePath());
                env.put("ASTRO_SUPERVISED", "1");             // exit code 75 = start me again
                env.put("ASTRO_EXIT_ON_STDIN_EOF", "1");      // stop when this app goes away
                env.put("ASTRO_SYNC_MIRROR", "off");
                env.put("ASTRO_ANDROID", "1");
                env.put("HOME", getFilesDir().getAbsolutePath());
                env.put("TMPDIR", tmp.getAbsolutePath());
                env.put("SSL_CERT_FILE", new File(home, "cacert.pem").getAbsolutePath());
                pb.redirectErrorStream(true);
                File log = new File(getFilesDir(), "engine.log");
                if (log.length() > 1_500_000) //noinspection ResultOfMethodCallIgnored
                    log.delete();
                pb.redirectOutput(ProcessBuilder.Redirect.appendTo(log));
                final Process p = pb.start();
                engine = p;      // its stdin stays open on purpose (see ASTRO_EXIT_ON_STDIN_EOF)

                waitReady(p);
                ui.post(this::loadApp);

                int code = p.waitFor();
                if (stopping || engine != p) return;
                if (code == 75) { ui.post(this::startEngine); return; }   // restart requested (new keys)
                ui.post(() -> showSplash(getString(R.string.engine_failed) + " (" + code + ")", true, logTail(log)));
            } catch (Exception e) {
                ui.post(() -> showSplash(getString(R.string.engine_failed), true, e + "\n\n" + logTail(new File(getFilesDir(), "engine.log"))));
            }
        }, "astro-engine").start();
    }

    private void waitReady(Process p) throws Exception {
        long deadline = System.currentTimeMillis() + 30_000;
        while (System.currentTimeMillis() < deadline) {
            if (!p.isAlive()) throw new IOException("The engine exited during start (code " + p.exitValue() + ")");
            try {
                HttpURLConnection c = (HttpURLConnection) new URL("http://127.0.0.1:" + port + "/api/auth/state").openConnection();
                c.setConnectTimeout(500);
                c.setReadTimeout(1500);
                c.setRequestProperty("X-Astro-Client", "1");
                int code = c.getResponseCode();
                c.disconnect();
                if (code == 200) return;
            } catch (IOException ignored) {
                // not listening yet
            }
            Thread.sleep(150);
        }
        throw new IOException("The engine did not start within 30 seconds");
    }

    private void loadApp() {
        String origin = "http://127.0.0.1:" + port;
        CookieManager cm = CookieManager.getInstance();
        cm.setAcceptCookie(true);
        cm.setCookie(origin, "astro_session=" + token + "; Path=/; SameSite=Lax; HttpOnly");
        cm.flush();
        web.loadUrl(origin + "/");
    }

    private void stopEngine() {
        stopping = true;
        new Thread(this::stopEngineNow, "astro-engine-stop").start();
    }

    /** Closing stdin asks the engine to stop cleanly (it stops its helpers too); it is killed after 3 seconds. */
    private void stopEngineNow() {
        Process p = engine;
        engine = null;
        if (p == null) return;
        try { p.getOutputStream().close(); } catch (IOException ignored) { /* already gone */ }
        long deadline = System.currentTimeMillis() + 3000;
        while (p.isAlive() && System.currentTimeMillis() < deadline) {
            try { Thread.sleep(50); } catch (InterruptedException e) { break; }
        }
        if (p.isAlive()) p.destroyForcibly();
    }

    /** Unpacks web/, providers.json, edition.json and cacert.pem from the APK after installs and updates. */
    private void preparePayload(File home) throws Exception {
        PackageInfo pi = getPackageManager().getPackageInfo(getPackageName(), 0);
        String stamp = pi.versionName + ":" + pi.lastUpdateTime;
        File stampFile = new File(home, ".payload");
        if (stampFile.exists() && stamp.equals(readText(stampFile)) && new File(home, "web/index.html").exists()) return;
        deleteTree(new File(home, "web"));
        //noinspection ResultOfMethodCallIgnored
        home.mkdirs();
        AssetManager am = getAssets();
        copyAssets(am, "payload", home);
        try (InputStream in = am.open("cacert.pem"); OutputStream out = new FileOutputStream(new File(home, "cacert.pem"))) {
            pipe(in, out);
        }
        try (OutputStream out = new FileOutputStream(stampFile)) {
            out.write(stamp.getBytes(StandardCharsets.UTF_8));
        }
    }

    private void copyAssets(AssetManager am, String path, File dest) throws IOException {
        String[] kids = am.list(path);
        if (kids == null || kids.length == 0) {
            try (InputStream in = am.open(path); OutputStream out = new FileOutputStream(dest)) { pipe(in, out); }
            return;
        }
        //noinspection ResultOfMethodCallIgnored
        dest.mkdirs();
        for (String k : kids) copyAssets(am, path + "/" + k, new File(dest, k));
    }

    // ---------------------------------------------------------------------------------------------------------
    // Sharing text to the app, files, links
    // ---------------------------------------------------------------------------------------------------------
    private void handleIntent(Intent intent) {
        if (intent == null || !Intent.ACTION_SEND.equals(intent.getAction())) return;
        CharSequence t = intent.getCharSequenceExtra(Intent.EXTRA_TEXT);
        if (t == null || t.length() == 0) return;
        pendingShare = t.toString();
        if (t.length() > 12000) pendingShare = pendingShare.substring(0, 12000);
        if (pageReady) deliverShare();
    }

    private void deliverShare() {
        if (pendingShare == null) return;
        String text = JSONObject.quote(pendingShare);
        pendingShare = null;
        js("(function(t){var i=document.getElementById('message-input');if(!i)return;i.value=i.value?i.value+'\\n'+t:t;"
                + "i.dispatchEvent(new Event('input',{bubbles:true}));i.focus();})(" + text + ")");
    }

    private void openExternal(String url) {
        Uri u = Uri.parse(url);
        String scheme = u.getScheme() == null ? "" : u.getScheme().toLowerCase(Locale.ROOT);
        if (!scheme.equals("https") && !scheme.equals("http") && !scheme.equals("mailto") && !scheme.equals("tel")) return;
        try {
            startActivity(new Intent(Intent.ACTION_VIEW, u).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        } catch (ActivityNotFoundException ignored) {
            // nothing can open it
        }
    }

    private JSONObject saveDownload(String name, String mime, byte[] bytes) throws Exception {
        String safe = name.replaceAll("[\\\\/:*?\"<>|\\p{Cntrl}]", "_").trim();
        if (safe.isEmpty() || safe.startsWith(".")) safe = "download" + safe;
        if (safe.length() > 120) safe = safe.substring(safe.length() - 120);
        JSONObject r = new JSONObject();
        if (Build.VERSION.SDK_INT >= 29) {
            ContentValues cv = new ContentValues();
            cv.put(MediaStore.MediaColumns.DISPLAY_NAME, safe);
            cv.put(MediaStore.MediaColumns.MIME_TYPE, mime == null || mime.isEmpty() ? "application/octet-stream" : mime);
            cv.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/A.S.T.R.O AI");
            ContentResolver cr = getContentResolver();
            Uri uri = cr.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, cv);
            if (uri == null) throw new IOException("Android refused to create the file");
            try (OutputStream out = cr.openOutputStream(uri)) {
                if (out == null) throw new IOException("Could not open the file");
                out.write(bytes);
            }
            r.put("name", "Download/A.S.T.R.O AI/" + safe);
        } else {
            File dir = getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS);
            if (dir == null) throw new IOException("No storage available");
            //noinspection ResultOfMethodCallIgnored
            dir.mkdirs();
            File f = new File(dir, safe);
            try (OutputStream out = new FileOutputStream(f)) { out.write(bytes); }
            r.put("name", f.getAbsolutePath());
        }
        r.put("ok", true);
        return r;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_FILE || fileCallback == null) return;
        Uri[] result = null;
        if (resultCode == RESULT_OK && data != null) {
            if (data.getClipData() != null) {
                int n = data.getClipData().getItemCount();
                result = new Uri[n];
                for (int i = 0; i < n; i++) result[i] = data.getClipData().getItemAt(i).getUri();
            } else if (data.getData() != null) {
                result = new Uri[]{data.getData()};
            }
        }
        fileCallback.onReceiveValue(result);
        fileCallback = null;
    }

    // ---------------------------------------------------------------------------------------------------------
    // Microphone permission
    // ---------------------------------------------------------------------------------------------------------
    private boolean hasMic() {
        return checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED;
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        if (requestCode != REQ_MIC) return;
        boolean ok = results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED;
        if (pendingWebPermission != null) {
            if (ok) pendingWebPermission.grant(new String[]{PermissionRequest.RESOURCE_AUDIO_CAPTURE});
            else pendingWebPermission.deny();
            pendingWebPermission = null;
        }
        Runnable r = pendingMic;
        pendingMic = null;
        if (r != null) {
            if (ok) r.run();
            else {
                Toast.makeText(this, R.string.mic_denied, Toast.LENGTH_LONG).show();
                String id = sttId;
                sttId = null;
                if (id != null) { sttEvent(id, "error", "not-allowed", null, false); sttEvent(id, "end", null, null, false); }
            }
        }
    }

    // ---------------------------------------------------------------------------------------------------------
    // Speech: Android TextToSpeech and SpeechRecognizer behind the Web Speech API (web/android.js)
    // ---------------------------------------------------------------------------------------------------------
    private void ttsEvent(String id, String ev, String error) {
        try {
            JSONObject e = new JSONObject().put("id", id).put("ev", ev);
            if (error != null) e.put("error", error);
            emit(e);
        } catch (Exception ignored) { /* JSON cannot fail here */ }
    }

    /** Utterance ids are "<id>#<part>/<parts>"; long text is spoken in parts and reported as one utterance. */
    private final class TtsListener extends UtteranceProgressListener {
        private String base(String uid) { int i = uid.indexOf('#'); return i < 0 ? uid : uid.substring(0, i); }
        private boolean first(String uid) { return uid.contains("#0/"); }
        private boolean last(String uid) {
            int h = uid.indexOf('#'), s = uid.indexOf('/');
            if (h < 0 || s < 0) return true;
            return Integer.parseInt(uid.substring(h + 1, s)) == Integer.parseInt(uid.substring(s + 1)) - 1;
        }
        @Override public void onStart(String uid) { if (first(uid)) ttsEvent(base(uid), "start", null); }
        @Override public void onDone(String uid) { if (last(uid)) ttsEvent(base(uid), "end", null); }
        @Override @SuppressWarnings("deprecation") public void onError(String uid) { ttsEvent(base(uid), "error", "synthesis-failed"); }
        @Override public void onError(String uid, int code) { ttsEvent(base(uid), "error", "synthesis-failed"); }
        @Override public void onStop(String uid, boolean interrupted) { if (last(uid)) ttsEvent(base(uid), "end", null); }
        @Override public void onRangeStart(String uid, int start, int end, int frame) { ttsEvent(base(uid), "boundary", null); }
    }

    private void speak(String id, String text, String lang, double rate, String voiceName) {
        if (!ttsReady) { ttsEvent(id, "error", "synthesis-unavailable"); return; }
        Voice chosen = null;
        if (voiceName != null && !voiceName.isEmpty()) {
            try { for (Voice v : tts.getVoices()) if (v.getName().equals(voiceName)) { chosen = v; break; } } catch (Exception ignored) { /* engine busy */ }
        }
        if (chosen != null) tts.setVoice(chosen);
        else tts.setLanguage(Locale.forLanguageTag(lang == null || lang.isEmpty() ? "en-US" : lang));
        tts.setSpeechRate((float) Math.max(0.3, Math.min(3.0, rate)));
        int max = Math.max(500, TextToSpeech.getMaxSpeechInputLength() - 100);
        List<String> parts = new ArrayList<>();
        String rest = text == null ? "" : text;
        while (rest.length() > max) {
            int cut = Math.max(rest.lastIndexOf(". ", max), rest.lastIndexOf('\n', max));
            if (cut < max / 2) cut = rest.lastIndexOf(' ', max);
            if (cut < max / 2) cut = max;
            parts.add(rest.substring(0, cut + 1));
            rest = rest.substring(cut + 1);
        }
        parts.add(rest);
        for (int i = 0; i < parts.size(); i++) {
            int mode = i == 0 ? TextToSpeech.QUEUE_FLUSH : TextToSpeech.QUEUE_ADD;
            if (tts.speak(parts.get(i), mode, null, id + "#" + i + "/" + parts.size()) != TextToSpeech.SUCCESS) {
                ttsEvent(id, "error", "synthesis-failed");
                return;
            }
        }
    }

    private void sttEvent(String id, String ev, String error, String text, boolean fin) {
        try {
            JSONObject e = new JSONObject().put("id", id).put("ev", ev);
            if (error != null) e.put("error", error);
            if (text != null) e.put("text", text).put("final", fin);
            emit(e);
        } catch (Exception ignored) { /* JSON cannot fail here */ }
    }

    private static String sttError(int code) {
        switch (code) {
            case SpeechRecognizer.ERROR_NO_MATCH:
            case SpeechRecognizer.ERROR_SPEECH_TIMEOUT: return "no-speech";
            case SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS: return "not-allowed";
            case SpeechRecognizer.ERROR_NETWORK:
            case SpeechRecognizer.ERROR_NETWORK_TIMEOUT:
            case SpeechRecognizer.ERROR_SERVER: return "network";
            case SpeechRecognizer.ERROR_AUDIO: return "audio-capture";
            case 12: /* ERROR_LANGUAGE_NOT_SUPPORTED */
            case 13: /* ERROR_LANGUAGE_UNAVAILABLE */ return "language-not-supported";
            default: return "aborted";
        }
    }

    private void listen(String id, String lang, boolean interim) {
        if (sttId != null && stt != null) {           // one session at a time: end the old one
            String old = sttId;
            stt.cancel();
            sttEvent(old, "end", null, null, false);
        }
        sttId = id;
        if (!hasMic()) {
            pendingMic = () -> listen(id, lang, interim);
            requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, REQ_MIC);
            return;
        }
        if (stt == null) {
            stt = SpeechRecognizer.createSpeechRecognizer(this);
            stt.setRecognitionListener(new RecognitionListener() {
                @Override public void onReadyForSpeech(Bundle b) { if (sttId != null) sttEvent(sttId, "start", null, null, false); }
                @Override public void onBeginningOfSpeech() { if (sttId != null) sttEvent(sttId, "speechstart", null, null, false); }
                @Override public void onRmsChanged(float v) { }
                @Override public void onBufferReceived(byte[] b) { }
                @Override public void onEndOfSpeech() { if (sttId != null) sttEvent(sttId, "speechend", null, null, false); }
                @Override public void onError(int code) {
                    String cur = sttId;
                    sttId = null;
                    if (cur == null) return;
                    sttEvent(cur, "error", sttError(code), null, false);
                    sttEvent(cur, "end", null, null, false);
                }
                @Override public void onResults(Bundle b) {
                    String cur = sttId;
                    sttId = null;
                    if (cur == null) return;
                    ArrayList<String> r = b.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
                    sttEvent(cur, "result", null, r == null || r.isEmpty() ? "" : r.get(0), true);
                    sttEvent(cur, "end", null, null, false);
                }
                @Override public void onPartialResults(Bundle b) {
                    ArrayList<String> r = b.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
                    if (sttId != null && r != null && !r.isEmpty() && !r.get(0).isEmpty()) sttEvent(sttId, "result", null, r.get(0), false);
                }
                @Override public void onEvent(int t, Bundle b) { }
            });
        }
        Intent i = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
        i.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
        i.putExtra(RecognizerIntent.EXTRA_LANGUAGE, lang == null || lang.isEmpty() ? Locale.getDefault().toLanguageTag() : lang);
        i.putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, interim);
        i.putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 1);
        i.putExtra(RecognizerIntent.EXTRA_CALLING_PACKAGE, getPackageName());
        stt.startListening(i);
    }

    // ---------------------------------------------------------------------------------------------------------
    // The page's bridge. Android adds it to every frame - including the frames that show code you run - so the
    // calls that touch the microphone, files or other apps need the secret that only the app page itself gets
    // (claimed once per page load, before any frame exists).
    // ---------------------------------------------------------------------------------------------------------
    private boolean nonceOk(String n) {
        String cur = pageNonce;
        return cur != null && n != null && nonceClaimed && cur.equals(n);
    }

    private final class Bridge {
        @JavascriptInterface
        public String info() {
            try {
                return new JSONObject()
                        .put("platform", "android").put("edition", "clean").put("arch", "arm64")
                        .put("version", versionName()).put("sdk", Build.VERSION.SDK_INT)
                        .put("device", Build.MANUFACTURER + " " + Build.MODEL).toString();
            } catch (Exception e) {
                return "{}";
            }
        }

        @JavascriptInterface
        public synchronized String claim() {
            if (nonceClaimed) return "";
            nonceClaimed = true;
            return pageNonce == null ? "" : pageNonce;
        }

        @JavascriptInterface
        public void openExternal(String nonce, String url) {
            if (!nonceOk(nonce) || url == null) return;
            ui.post(() -> MainActivity.this.openExternal(url));
        }

        @JavascriptInterface
        public String saveFile(String nonce, String name, String mime, String b64) {
            try {
                if (!nonceOk(nonce)) throw new SecurityException("not allowed from this frame");
                return saveDownload(name == null ? "download" : name, mime, Base64.decode(b64 == null ? "" : b64, Base64.DEFAULT)).toString();
            } catch (Exception e) {
                return "{\"ok\":false,\"error\":" + JSONObject.quote(String.valueOf(e.getMessage())) + "}";
            }
        }

        @JavascriptInterface
        public void setTheme(String theme) {
            boolean dark = "dark".equals(theme) || (!"light".equals(theme) && nightMode());
            ui.post(() -> {
                int bg = dark ? 0xFF0A0A0C : 0xFFFBFBFA;
                root.setBackgroundColor(bg);
                if (Build.VERSION.SDK_INT >= 30) {
                    WindowInsetsController c = getWindow().getInsetsController();
                    if (c != null) {
                        int mask = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
                        c.setSystemBarsAppearance(dark ? 0 : mask, mask);
                    }
                } else {
                    View d = getWindow().getDecorView();
                    int f = d.getSystemUiVisibility();
                    f = dark ? (f & ~View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR) : (f | View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR);
                    d.setSystemUiVisibility(f);
                }
            });
        }

        @JavascriptInterface
        public String ttsVoices() {
            JSONArray out = new JSONArray();
            if (!ttsReady) return out.toString();
            try {
                Set<Voice> voices = tts.getVoices();
                Voice def = tts.getDefaultVoice();
                for (Voice v : voices) {
                    if (v.getFeatures() != null && v.getFeatures().contains(TextToSpeech.Engine.KEY_FEATURE_NOT_INSTALLED)) continue;
                    out.put(new JSONObject().put("name", v.getName()).put("lang", v.getLocale().toLanguageTag())
                            .put("local", !v.isNetworkConnectionRequired()).put("default", def != null && def.getName().equals(v.getName())));
                    if (out.length() >= 300) break;
                }
            } catch (Exception ignored) { /* engine busy: no voices yet */ }
            return out.toString();
        }

        @JavascriptInterface
        public void ttsSpeak(String id, String text, String lang, double rate, String voice) {
            ui.post(() -> speak(id, text, lang, rate, voice));
        }

        @JavascriptInterface
        public void ttsStop() {
            ui.post(() -> { if (ttsReady) tts.stop(); });
        }

        @JavascriptInterface
        public String sttAvailable() {
            return SpeechRecognizer.isRecognitionAvailable(MainActivity.this) ? "true" : "false";
        }

        @JavascriptInterface
        public void sttStart(String nonce, String id, String lang, boolean continuous, boolean interim) {
            if (!nonceOk(nonce)) { sttEvent(id, "error", "not-allowed", null, false); sttEvent(id, "end", null, null, false); return; }
            ui.post(() -> listen(id, lang, interim));
        }

        @JavascriptInterface
        public void sttStop(String id) {
            ui.post(() -> { if (stt != null && id != null && id.equals(sttId)) stt.stopListening(); });
        }

        @JavascriptInterface
        public void sttAbort(String id) {
            ui.post(() -> {
                if (stt == null || id == null || !id.equals(sttId)) return;
                sttId = null;
                stt.cancel();
                sttEvent(id, "end", null, null, false);
            });
        }
    }

    // ---------------------------------------------------------------------------------------------------------
    // Small helpers
    // ---------------------------------------------------------------------------------------------------------
    private String versionName() {
        try {
            return getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
        } catch (Exception e) {
            return "?";
        }
    }

    private String randomHex(int bytes) {
        byte[] b = new byte[bytes];
        rng.nextBytes(b);
        StringBuilder sb = new StringBuilder(bytes * 2);
        for (byte x : b) sb.append(String.format(Locale.ROOT, "%02x", x));
        return sb.toString();
    }

    private static int freePort() throws IOException {
        try (ServerSocket s = new ServerSocket(0, 1, InetAddress.getByName("127.0.0.1"))) {
            return s.getLocalPort();
        }
    }

    private static void pipe(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[64 * 1024];
        int n;
        while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
    }

    private static String readText(File f) {
        try (InputStream in = new FileInputStream(f)) {
            byte[] b = new byte[(int) Math.min(f.length(), 4096)];
            int n = in.read(b);
            return n <= 0 ? "" : new String(b, 0, n, StandardCharsets.UTF_8);
        } catch (IOException e) {
            return "";
        }
    }

    private static String logTail(File f) {
        try (RandomAccessFile r = new RandomAccessFile(f, "r")) {
            long len = r.length();
            long from = Math.max(0, len - 4000);
            byte[] b = new byte[(int) (len - from)];
            r.seek(from);
            r.readFully(b);
            return new String(b, StandardCharsets.UTF_8);
        } catch (IOException e) {
            return "";
        }
    }

    private static void deleteTree(File f) {
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteTree(k);
        //noinspection ResultOfMethodCallIgnored
        f.delete();
    }
}

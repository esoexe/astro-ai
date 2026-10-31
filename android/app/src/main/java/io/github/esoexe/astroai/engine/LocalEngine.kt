package io.github.esoexe.astroai.engine

import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import io.github.esoexe.astroai.core.randomHex
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.InetAddress
import java.net.ServerSocket
import java.net.URL

/**
 * The A.S.T.R.O AI engine (the same C++ server as on the computer) running inside the phone.
 *
 * It is packaged as lib/arm64-v8a/libastro_engine.so because the native library folder is the only place
 * an Android app may start its own programs from. It listens on 127.0.0.1 only, on a free port, and accepts
 * only requests that carry this launch's random token, so other apps on the phone cannot use it. Its stdin
 * stays open: when this app's process ends, the pipe closes and the engine stops itself.
 */
class LocalEngine(private val ctx: Context) {
    sealed interface State {
        data object Stopped : State
        data object Starting : State
        data class Ready(val base: String, val token: String) : State
        data class Failed(val message: String, val log: String) : State
    }

    private val _state = MutableStateFlow<State>(State.Stopped)
    val state: StateFlow<State> = _state
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    @Volatile private var process: Process? = null
    @Volatile private var stopping = false
    private val logFile get() = File(ctx.filesDir, "engine.log")

    /** Starts the engine unless it is already starting or running. */
    fun start() {
        val s = _state.value
        if (s is State.Starting || (s is State.Ready && process?.isAlive == true)) return
        stopping = false
        _state.value = State.Starting
        scope.launch { run() }
    }

    fun restart() { scope.launch { stopNow(); start() } }

    fun stop() { stopping = true; scope.launch { stopNow(); _state.value = State.Stopped } }

    private suspend fun run() {
        try {
            val home = File(ctx.filesDir, "app")
            preparePayload(home)
            val data = File(ctx.filesDir, "data").apply { mkdirs() }   // same place as 1.8.0: keys and projects stay
            val tmp = File(ctx.cacheDir, "tmp").apply { mkdirs() }
            stopNow()
            val port = freePort()
            val token = randomHex(32)
            val exe = File(ctx.applicationInfo.nativeLibraryDir, "libastro_engine.so")
            if (!exe.exists()) throw IOException("The engine is missing from this APK ($exe)")
            val pb = ProcessBuilder(exe.absolutePath).directory(home)
            pb.environment().apply {
                put("PORT", port.toString())
                put("ASTRO_BIND", "127.0.0.1")
                put("ASTRO_AUTH", "off")                  // no password screen inside the phone...
                put("ASTRO_APP_TOKEN", token)             // ...but only this app gets in
                put("ASTRO_DATA_DIR", data.absolutePath)
                put("ASTRO_SUPERVISED", "1")              // exit code 75 = start me again (new keys)
                put("ASTRO_EXIT_ON_STDIN_EOF", "1")       // stop when this app goes away
                put("ASTRO_SYNC_MIRROR", "off")
                put("ASTRO_ANDROID", "1")
                put("HOME", ctx.filesDir.absolutePath)
                put("TMPDIR", tmp.absolutePath)
                put("SSL_CERT_FILE", File(home, "cacert.pem").absolutePath)
            }
            pb.redirectErrorStream(true)
            if (logFile.length() > 1_500_000) logFile.delete()
            pb.redirectOutput(ProcessBuilder.Redirect.appendTo(logFile))
            val p = pb.start()
            process = p
            val base = "http://127.0.0.1:$port"
            waitReady(p, base)
            _state.value = State.Ready(base, token)
            val code = p.waitFor()
            if (stopping || process !== p) return
            process = null
            if (code == 75) { _state.value = State.Starting; run(); return }
            _state.value = State.Failed("The engine stopped (code $code).", logTail())
        } catch (e: Exception) {
            if (!stopping) _state.value = State.Failed(e.message ?: e.toString(), logTail())
        }
    }

    private suspend fun waitReady(p: Process, base: String) {
        val deadline = System.currentTimeMillis() + 30_000
        while (System.currentTimeMillis() < deadline) {
            if (!p.isAlive) throw IOException("The engine exited during start (code ${p.exitValue()})")
            try {
                val c = URL("$base/api/auth/state").openConnection() as HttpURLConnection
                c.connectTimeout = 500; c.readTimeout = 1500
                c.setRequestProperty("X-Astro-Client", "android")
                val code = c.responseCode
                c.disconnect()
                if (code == 200) return
            } catch (_: IOException) { /* not listening yet */ }
            delay(150)
        }
        throw IOException("The engine did not start within 30 seconds")
    }

    /** Closing stdin asks the engine to stop cleanly (it stops its helpers too); it is killed after 3 seconds. */
    private fun stopNow() {
        val p = process ?: return
        process = null
        try { p.outputStream.close() } catch (_: IOException) { }
        val deadline = System.currentTimeMillis() + 3000
        while (p.isAlive && System.currentTimeMillis() < deadline) Thread.sleep(50)
        if (p.isAlive) p.destroyForcibly()
    }

    /** Unpacks providers.json, edition.json, the model ranking and the CA bundle after installs and updates. */
    private fun preparePayload(home: File) {
        val pi = if (Build.VERSION.SDK_INT >= 33) ctx.packageManager.getPackageInfo(ctx.packageName, PackageManager.PackageInfoFlags.of(0))
            else @Suppress("DEPRECATION") ctx.packageManager.getPackageInfo(ctx.packageName, 0)
        val stamp = "native:${pi.versionName}:${pi.lastUpdateTime}"
        val stampFile = File(home, ".payload")
        if (stampFile.exists() && stampFile.readText() == stamp && File(home, "providers.json").exists()) return
        File(home, "web").deleteRecursively()       // 1.8.0 shipped the web app here; the native app does not need it
        home.mkdirs()
        copyAssets("payload", home)
        ctx.assets.open("cacert.pem").use { input -> File(home, "cacert.pem").outputStream().use { input.copyTo(it) } }
        stampFile.writeText(stamp)
    }

    private fun copyAssets(path: String, dest: File) {
        val kids = ctx.assets.list(path)
        if (kids.isNullOrEmpty()) {
            ctx.assets.open(path).use { input -> dest.outputStream().use { input.copyTo(it) } }
            return
        }
        dest.mkdirs()
        for (k in kids) copyAssets("$path/$k", File(dest, k))
    }

    private fun freePort(): Int = ServerSocket(0, 1, InetAddress.getByName("127.0.0.1")).use { it.localPort }

    fun logTail(max: Int = 4000): String = try {
        val t = logFile.readText()
        if (t.length > max) t.substring(t.length - max) else t
    } catch (_: Exception) { "" }
}

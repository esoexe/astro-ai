package io.github.esoexe.astroai.net

import io.github.esoexe.astroai.core.str
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.Inet4Address
import java.net.Inet6Address
import java.net.InetAddress
import java.net.URL
import java.security.MessageDigest
import java.util.concurrent.ConcurrentHashMap

class ApiError(val status: Int, message: String, val body: JSONObject? = null) : IOException(message)

/**
 * The app's only network layer. Requests go to the current engine (the one inside the phone, or A.S.T.R.O AI on
 * your computer) with its token; anything else (the update check) goes out without credentials. Plain http is
 * refused unless the address is this phone or your own network (see network_security_config.xml).
 */
object Api {
    @Volatile var base: String = ""
        private set
    @Volatile var token: String = ""
        private set

    fun configure(base: String, token: String) { this.base = base.trimEnd('/'); this.token = token }
    val ready: Boolean get() = base.isNotEmpty()

    private val privateHosts = ConcurrentHashMap<String, Boolean>()

    /** True for loopback, private (RFC 1918), link-local, carrier-grade NAT (Tailscale) and IPv6 ULA addresses. */
    fun isPrivate(a: InetAddress): Boolean {
        if (a.isLoopbackAddress || a.isSiteLocalAddress || a.isLinkLocalAddress) return true
        val b = a.address
        if (a is Inet4Address) return (b[0].toInt() and 0xFF) == 100 && (b[1].toInt() and 0xC0) == 64
        if (a is Inet6Address) return (b[0].toInt() and 0xFE) == 0xFC
        return false
    }

    fun checkUrl(u: URL) {
        when (u.protocol) {
            "https" -> return
            "http" -> {
                val ok = privateHosts.getOrPut(u.host) {
                    try { InetAddress.getAllByName(u.host).let { list -> list.isNotEmpty() && list.all { isPrivate(it) } } } catch (_: Exception) { false }
                }
                if (!ok) throw ApiError(0, "Plain http is only allowed to this phone or your own network. Use https for ${u.host}.")
            }
            else -> throw ApiError(0, "Unsupported address: ${u.protocol}")
        }
    }

    private fun open(url: String, method: String, timeoutMs: Int, sse: Boolean = false): HttpURLConnection {
        val u = URL(url)
        checkUrl(u)
        val c = u.openConnection() as HttpURLConnection
        c.requestMethod = method
        c.connectTimeout = 10_000
        c.readTimeout = timeoutMs
        c.instanceFollowRedirects = true
        c.useCaches = false
        c.setRequestProperty("Accept", if (sse) "text/event-stream" else "application/json")
        // Credentials only ever go to the engine, never to another site.
        if (base.isNotEmpty() && url.startsWith("$base/")) {
            c.setRequestProperty("X-Astro-Client", "android")
            if (token.isNotEmpty()) c.setRequestProperty("X-Astro-Token", token)
        }
        return c
    }

    private fun url(path: String) = if (path.startsWith("http://") || path.startsWith("https://")) path else {
        if (base.isEmpty()) throw ApiError(0, "Not connected yet")
        base + path
    }

    private fun readAll(s: InputStream?): String = s?.use { it.readBytes().toString(Charsets.UTF_8) } ?: ""

    private fun parse(text: String): JSONObject {
        val t = text.trim()
        if (t.isEmpty()) return JSONObject()
        return when {
            t.startsWith("{") -> JSONObject(t)
            t.startsWith("[") -> JSONObject().put("items", JSONArray(t))
            else -> JSONObject().put("text", t)
        }
    }

    private fun errorOf(c: HttpURLConnection, code: Int): ApiError {
        val body = try { parse(readAll(c.errorStream)) } catch (_: Exception) { JSONObject() }
        val msg = body.str("error").ifEmpty { body.str("message") }.ifEmpty {
            when (code) {
                401 -> "Sign in again (session ended)"
                403 -> "Not allowed"
                404 -> "Not found"
                429 -> "Too many requests right now"
                else -> "Request failed ($code)"
            }
        }
        return ApiError(code, msg, body)
    }

    fun requestBlocking(method: String, path: String, body: JSONObject?, timeoutMs: Int, headers: Map<String, String> = emptyMap()): JSONObject {
        val c = open(url(path), method, timeoutMs)
        headers.forEach { (k, v) -> c.setRequestProperty(k, v) }
        try {
            if (body != null) {
                c.doOutput = true
                c.setRequestProperty("Content-Type", "application/json; charset=utf-8")
                c.outputStream.use { it.write(body.toString().toByteArray(Charsets.UTF_8)) }
            }
            val code = c.responseCode
            if (code !in 200..299) throw errorOf(c, code)
            return parse(readAll(c.inputStream))
        } finally { c.disconnect() }
    }

    suspend fun get(path: String, timeoutMs: Int = 20_000): JSONObject = withContext(Dispatchers.IO) { requestBlocking("GET", path, null, timeoutMs) }
    suspend fun post(path: String, body: JSONObject = JSONObject(), timeoutMs: Int = 120_000): JSONObject =
        withContext(Dispatchers.IO) { requestBlocking("POST", path, body, timeoutMs) }

    /** Raw bytes (zip export, files). */
    suspend fun bytes(path: String, body: JSONObject? = null, timeoutMs: Int = 120_000): ByteArray = withContext(Dispatchers.IO) {
        val c = open(url(path), if (body != null) "POST" else "GET", timeoutMs)
        try {
            if (body != null) {
                c.doOutput = true
                c.setRequestProperty("Content-Type", "application/json; charset=utf-8")
                c.outputStream.use { it.write(body.toString().toByteArray(Charsets.UTF_8)) }
            }
            val code = c.responseCode
            if (code !in 200..299) throw errorOf(c, code)
            c.inputStream.use { it.readBytes() }
        } finally { c.disconnect() }
    }

    /**
     * Server-sent events as a Flow of the JSON in each "data:" line. Cancelling the collector closes the
     * connection at once, which tells the engine to stop the models.
     */
    fun sse(path: String, body: JSONObject?, timeoutMs: Int = 25 * 60_000): Flow<JSONObject> = callbackFlow {
        val c = open(url(path), if (body != null) "POST" else "GET", timeoutMs, sse = true)
        val job = launch(Dispatchers.IO) {
            try {
                if (body != null) {
                    c.doOutput = true
                    c.setRequestProperty("Content-Type", "application/json; charset=utf-8")
                    c.outputStream.use { it.write(body.toString().toByteArray(Charsets.UTF_8)) }
                }
                val code = c.responseCode
                if (code !in 200..299) throw errorOf(c, code)
                c.inputStream.bufferedReader(Charsets.UTF_8).use { r ->
                    while (true) {
                        val line = r.readLine() ?: break
                        if (!line.startsWith("data:")) continue
                        val j = try { JSONObject(line.substring(5).trim()) } catch (_: Exception) { continue }
                        send(j)
                    }
                }
                close()
            } catch (e: Exception) {
                close(if (e is IOException) e else IOException(e.message ?: "Connection failed", e))
            }
        }
        awaitClose { job.cancel(); try { c.disconnect() } catch (_: Exception) { } }
    }.flowOn(Dispatchers.IO)

    /** Downloads an https file (updates) to [dest], checking its SHA-256. Returns the hex digest. */
    suspend fun download(url: String, dest: File, expectSha256: String?, onProgress: (Long, Long) -> Unit): String = withContext(Dispatchers.IO) {
        val c = open(url, "GET", 60_000)
        try {
            val code = c.responseCode
            if (code !in 200..299) throw errorOf(c, code)
            val total = c.contentLengthLong
            val md = MessageDigest.getInstance("SHA-256")
            dest.parentFile?.mkdirs()
            var done = 0L
            c.inputStream.use { input ->
                dest.outputStream().use { out ->
                    val buf = ByteArray(64 * 1024)
                    while (true) {
                        val n = input.read(buf)
                        if (n < 0) break
                        out.write(buf, 0, n); md.update(buf, 0, n); done += n
                        onProgress(done, total)
                    }
                }
            }
            val hex = md.digest().joinToString("") { "%02x".format(it) }
            if (!expectSha256.isNullOrBlank() && !hex.equals(expectSha256.trim(), ignoreCase = true)) {
                dest.delete()
                throw ApiError(0, "The download is damaged or was changed (SHA-256 does not match). Nothing was installed.")
            }
            hex
        } finally { c.disconnect() }
    }
}

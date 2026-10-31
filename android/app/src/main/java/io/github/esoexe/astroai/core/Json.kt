package io.github.esoexe.astroai.core

import org.json.JSONArray
import org.json.JSONObject
import java.security.SecureRandom

/** Small helpers over org.json (part of Android) so the app needs no serialization library. */

fun JSONObject.str(key: String, max: Int = Int.MAX_VALUE): String {
    if (!has(key) || isNull(key)) return ""
    val v = opt(key)
    val s = if (v is String) v else v?.toString() ?: ""
    return if (s.length > max) s.substring(0, max) else s
}

fun JSONObject.num(key: String, fallback: Double = 0.0): Double {
    val v = opt(key)
    return when (v) {
        is Number -> v.toDouble().takeIf { it.isFinite() } ?: fallback
        is String -> v.toDoubleOrNull() ?: fallback
        else -> fallback
    }
}

fun JSONObject.long(key: String, fallback: Long = 0L): Long = num(key, fallback.toDouble()).toLong()
fun JSONObject.int(key: String, fallback: Int = 0): Int = num(key, fallback.toDouble()).toInt()
fun JSONObject.bool(key: String, fallback: Boolean = false): Boolean = when (val v = opt(key)) {
    is Boolean -> v
    is Number -> v.toInt() != 0
    is String -> v == "true" || v == "1"
    else -> fallback
}

fun JSONObject.arr(key: String): JSONArray = optJSONArray(key) ?: JSONArray()
fun JSONObject.obj(key: String): JSONObject = optJSONObject(key) ?: JSONObject()

fun JSONArray.objects(): List<JSONObject> = (0 until length()).mapNotNull { optJSONObject(it) }
fun JSONArray.strings(): List<String> = (0 until length()).mapNotNull { i -> opt(i)?.takeIf { it != JSONObject.NULL }?.toString() }

/** Builds a JSONObject; null values are left out; lists, maps and nested objects are converted. */
fun jsonOf(vararg pairs: Pair<String, Any?>): JSONObject {
    val o = JSONObject()
    for ((k, v) in pairs) if (v != null) o.put(k, toJsonValue(v))
    return o
}

fun toJsonValue(v: Any?): Any? = when (v) {
    null -> JSONObject.NULL
    is JSONObject, is JSONArray, is String, is Boolean, is Int, is Long, is Double -> v
    is Float -> v.toDouble()
    is Number -> v.toDouble()
    is Map<*, *> -> JSONObject().also { o -> v.forEach { (k, x) -> if (x != null) o.put(k.toString(), toJsonValue(x)) } }
    is Iterable<*> -> JSONArray().also { a -> v.forEach { a.put(toJsonValue(it)) } }
    is Array<*> -> JSONArray().also { a -> v.forEach { a.put(toJsonValue(it)) } }
    else -> v.toString()
}

fun JSONObject.copy(): JSONObject = JSONObject(toString())

private val rng = SecureRandom()

/** Same shape as the web app's makeId(): short, random, URL-safe. */
fun makeId(): String {
    val chars = "abcdefghijklmnopqrstuvwxyz0123456789"
    val sb = StringBuilder(java.lang.Long.toString(System.currentTimeMillis(), 36))
    repeat(8) { sb.append(chars[rng.nextInt(chars.length)]) }
    return sb.toString()
}

fun randomHex(bytes: Int): String {
    val b = ByteArray(bytes)
    rng.nextBytes(b)
    return b.joinToString("") { "%02x".format(it) }
}

fun clip(s: String, max: Int): String = if (s.length > max) s.substring(0, max) else s

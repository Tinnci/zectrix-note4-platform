package dev.zectrix.note4.companion

import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URI
import java.net.URLEncoder
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.CodingErrorAction
import kotlin.math.roundToInt

// Weather is a dated snapshot. It never changes the clock or implies live conditions.
data class WeatherSnapshot(val place: String, val deciCelsius: Int, val code: Int,
                           val observedAt: Long, val expiresAt: Long)
object WeatherSync {
    const val KEY = 0x0102
    private val codes = setOf(0, 1, 2, 3, 45, 48, 51, 53, 55, 56, 57, 61, 63, 65, 66, 67,
        71, 73, 75, 77, 80, 81, 82, 85, 86, 95, 96, 99)

    fun encode(value: WeatherSnapshot): ByteArray {
        val name = value.place.toByteArray(Charsets.UTF_8)
        require(name.size in 1..48 && name.toString(Charsets.UTF_8) == value.place &&
            value.place.none { it.code < 32 || it.code == 127 })
        require(value.code in codes && value.deciCelsius in -1000..1000)
        require(value.observedAt in 946684800..4102444799L && value.expiresAt > value.observedAt &&
            value.expiresAt <= 4102444800L && value.expiresAt - value.observedAt <= 21600)
        return ByteBuffer.allocate(13 + name.size).order(ByteOrder.LITTLE_ENDIAN)
            .put(1).put(value.code.toByte()).putShort(value.deciCelsius.toShort())
            .putInt(value.observedAt.toInt()).putInt(value.expiresAt.toInt()).put(name.size.toByte()).put(name).array()
    }

    fun decode(bytes: ByteArray): WeatherSnapshot? = runCatching {
        require(bytes.size in 14..61)
        val input = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        require(input.get().toInt() == 1)
        val code = input.get().toInt() and 255
        val temperature = input.short.toInt()
        val observed = input.int.toLong() and 0xffff_ffffL
        val expires = input.int.toLong() and 0xffff_ffffL
        require((input.get().toInt() and 255) == input.remaining())
        val name = Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT).decode(input).toString()
        WeatherSnapshot(name, temperature, code, observed, expires).also { require(encode(it).contentEquals(bytes)) }
    }.getOrNull()

    fun fresh(value: WeatherSnapshot, now: Long) = now >= value.observedAt - 300 && now < value.expiresAt
    fun describe(code: Int): String = when (code) {
        0 -> "Clear"
        1, 2, 3 -> "Cloudy"
        45, 48 -> "Fog"
        51, 53, 55, 56, 57 -> "Drizzle"
        71, 73, 75, 77, 85, 86 -> "Snow"
        95, 96, 99 -> "Thunderstorm"
        else -> "Rain"
    }
}

data class WeatherPlace(val name: String, val region: String, val latitude: Double, val longitude: Double)

/** Explicit city lookup, without location permission or background polling. */
class PhoneWeatherClient : AutoCloseable {
    @Volatile private var active: HttpURLConnection? = null
    @Volatile private var cancelled = false

    fun search(city: String): List<WeatherPlace> {
        require(city.trim().toByteArray(Charsets.UTF_8).size in 1..80) { "Enter a city name" }
        val response = get("https://geocoding-api.open-meteo.com/v1/search?count=5&language=en&format=json&name=" +
            URLEncoder.encode(city.trim(), "UTF-8"))
        val entries = response.optJSONArray("results") ?: return emptyList()
        require(entries.length() <= 5)
        return (0 until entries.length()).map { index ->
            val item = entries.getJSONObject(index)
            val name = item.getString("name")
            val latitude = item.getDouble("latitude")
            val longitude = item.getDouble("longitude")
            require(name.toByteArray(Charsets.UTF_8).size in 1..48 && latitude in -90.0..90.0 && longitude in -180.0..180.0)
            WeatherPlace(name, listOf(item.optString("admin1"), item.optString("country")).filter { it.isNotBlank() }.joinToString(", ").take(100), latitude, longitude)
        }
    }

    fun current(place: WeatherPlace, nowSeconds: Long): WeatherSnapshot {
        require(place.latitude in -90.0..90.0 && place.longitude in -180.0..180.0)
        val item = get("https://api.open-meteo.com/v1/forecast?latitude=${place.latitude}&longitude=${place.longitude}" +
            "&current=temperature_2m,weather_code&temperature_unit=celsius&timeformat=unixtime&forecast_days=1").getJSONObject("current")
        val observed = item.getLong("time")
        val temperature = item.getDouble("temperature_2m")
        require(temperature.isFinite() && observed in nowSeconds - 7200..nowSeconds + 300) { "Weather service returned an outdated observation" }
        return WeatherSnapshot(place.name, (temperature * 10).roundToInt(), item.getInt("weather_code"),
            observed, observed + 21600).also { WeatherSync.encode(it) }
    }

    private fun get(url: String): JSONObject {
        check(!cancelled && !Thread.currentThread().isInterrupted) { "Weather request cancelled" }
        val connection = URI(url).toURL().openConnection() as HttpURLConnection
        active = connection
        try {
            check(!cancelled)
            connection.instanceFollowRedirects = false
            connection.connectTimeout = 8000
            connection.readTimeout = 10000
            connection.useCaches = false
            connection.setRequestProperty("Accept", "application/json")
            require(connection.responseCode == 200) { "Weather service unavailable; try again later" }
            return connection.inputStream.use { input ->
                val output = java.io.ByteArrayOutputStream()
                val buffer = ByteArray(2048)
                val deadline = System.nanoTime() + 20_000_000_000L
                while (true) {
                    check(!cancelled && !Thread.currentThread().isInterrupted && System.nanoTime() < deadline)
                    val read = input.read(buffer)
                    if (read < 0) break
                    require(output.size() + read <= 32768) { "Weather response is too large" }
                    output.write(buffer, 0, read)
                }
                JSONObject(output.toString("UTF-8"))
            }
        } finally { active = null; connection.disconnect() }
    }

    override fun close() { cancelled = true; active?.disconnect() }
}

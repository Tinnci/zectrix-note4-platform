package dev.zectrix.note4.companion

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import java.net.HttpURLConnection
import java.net.InetAddress
import java.net.URL

/** A Note4 hotspot has no Internet validation and may not be Android's default network. */
@Suppress("DEPRECATION")
fun openLocalTransfer(context: Context, url: URL): HttpURLConnection {
    val connectivity = context.getSystemService(ConnectivityManager::class.java)
    val destination = InetAddress.getByName(url.host)
    val candidates = connectivity.allNetworks.mapNotNull { network ->
        val capabilities = connectivity.getNetworkCapabilities(network) ?: return@mapNotNull null
        if (!capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) return@mapNotNull null
        val prefix = connectivity.getLinkProperties(network)?.routes
            ?.filter { it.destination.prefixLength > 0 && it.matches(destination) }
            ?.maxOfOrNull { it.destination.prefixLength } ?: return@mapNotNull null
        network to prefix
    }
    val longest = candidates.maxOfOrNull { it.second }
    val matching = candidates.filter { it.second == longest }
    require(matching.size <= 1) { "More than one Wi-Fi route matches Note4; choose its network again" }
    return (matching.singleOrNull()?.first?.openConnection(url) ?: url.openConnection()) as HttpURLConnection
}

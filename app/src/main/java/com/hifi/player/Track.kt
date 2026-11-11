package com.hifi.player

import android.net.Uri

data class Track(
    val uri: Uri,
    val title: String,
    val artist: String = "Unknown Artist",
    val durationMs: Long = 0L
)

package com.localacr.demo

data class TrackInfo(
    val title: String,
    val artist: String,
)

/** Maps trigger IDs to display info. Source format: one `triggerId<TAB>title<TAB>artist` line per track. */
class TrackCatalog(private val tracks: Map<String, TrackInfo>) {
    val size: Int get() = tracks.size

    fun lookup(triggerId: String): TrackInfo = tracks[triggerId] ?: TrackInfo(title = triggerId, artist = "")

    companion object {
        fun parse(tsv: String): TrackCatalog =
            TrackCatalog(
                tsv.lineSequence()
                    .filter { it.isNotBlank() }
                    .map { it.split('\t') }
                    .filter { it.size >= 2 }
                    .associate { columns -> columns[0] to TrackInfo(columns[1], columns.getOrElse(2) { "" }) },
            )
    }
}

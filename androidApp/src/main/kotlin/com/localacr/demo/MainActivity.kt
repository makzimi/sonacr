package com.localacr.demo

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import java.io.File

private const val DemoDatabaseAsset = "tracks.lacrdb"
private const val CatalogAsset = "catalog.tsv"

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    LocalAcrDemoApp()
                }
            }
        }
    }
}

@Composable
fun LocalAcrDemoApp() {
    val context = LocalContext.current
    val databasePath = remember {
        runCatching { context.copyAssetToFiles(DemoDatabaseAsset).absolutePath }.getOrNull()
    }
    if (databasePath == null) {
        Text(
            "Missing $DemoDatabaseAsset. Run tools/demo/build_demo_assets.py and rebuild the app.",
            modifier = Modifier.padding(24.dp),
        )
        return
    }
    var permissionGranted by remember {
        mutableStateOf(context.hasRecordAudioPermission())
    }
    var state by remember { mutableStateOf(DemoState()) }
    val controller = remember {
        val catalog = runCatching {
            TrackCatalog.parse(context.assets.open(CatalogAsset).bufferedReader().use { it.readText() })
        }.getOrDefault(TrackCatalog(emptyMap()))
        DemoController(
            SharedLocalAcrDemoRecognizer(databasePath),
            catalog,
            onStateChanged = { state = it },
        )
    }

    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { granted ->
        permissionGranted = granted
        controller.onScreenVisible(granted)
    }

    LaunchedEffect(permissionGranted) {
        controller.onScreenVisible(permissionGranted)
    }

    DisposableEffect(Unit) {
        onDispose {
            controller.onScreenHidden()
        }
    }

    DemoScreen(
        state = state,
        permissionGranted = permissionGranted,
        onRequestPermission = { permissionLauncher.launch(Manifest.permission.RECORD_AUDIO) },
    )
}

@Composable
fun DemoScreen(
    state: DemoState,
    permissionGranted: Boolean,
    onRequestPermission: () -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        verticalArrangement = Arrangement.Center,
    ) {
        Text("Local ACR", style = MaterialTheme.typography.headlineMedium)
        Spacer(Modifier.height(8.dp))
        Text("Play one of the bundled tracks nearby. Recognition runs fully on this device.")
        Spacer(Modifier.height(24.dp))
        Text("Status: ${state.status}")

        if (!permissionGranted) {
            Spacer(Modifier.height(16.dp))
            Button(onClick = onRequestPermission) {
                Text("Allow microphone")
            }
        }

        state.errorMessage?.let { message ->
            Spacer(Modifier.height(16.dp))
            Text("Recognition error: $message", color = MaterialTheme.colorScheme.error)
        }

        state.nowPlaying?.let { track ->
            Spacer(Modifier.height(24.dp))
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("Now playing", style = MaterialTheme.typography.labelLarge)
                    Spacer(Modifier.height(8.dp))
                    Text(track.title, style = MaterialTheme.typography.titleLarge)
                    if (track.artist.isNotBlank()) {
                        Text(track.artist)
                    }
                    Spacer(Modifier.height(8.dp))
                    Text("at ${formatPosition(track.matchedPositionMs)} · confidence ${(track.confidence * 100).toInt()}%")
                }
            }
        }
    }
}

private fun formatPosition(positionMs: Long): String {
    val totalSeconds = positionMs / 1_000
    return "%d:%02d".format(totalSeconds / 60, totalSeconds % 60)
}

private fun Context.hasRecordAudioPermission(): Boolean =
    ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED

private fun Context.copyAssetToFiles(assetName: String): File {
    val target = File(filesDir, assetName)
    assets.open(assetName).use { input ->
        target.outputStream().use { output ->
            input.copyTo(output)
        }
    }
    return target
}

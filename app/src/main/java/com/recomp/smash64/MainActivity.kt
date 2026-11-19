package com.recomp.smash64

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File

private val ExpectedSha256 =
    "15592E79D3C5295CEF4371D4992F0BD25BEC2102FC29644C93E682F7EA99EF3D"

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val gameDir = File(filesDir, "games/nale").apply { mkdirs() }

        setContent {
            MaterialTheme(
                colorScheme = darkColorScheme(
                    background = Color(0xFF0D0F14),
                    surface = Color(0xFF161A23),
                    primary = Color(0xFFE53935),
                    secondary = Color(0xFF29B6F6),
                    onBackground = Color(0xFFF2F4F8),
                    onSurface = Color(0xFFF2F4F8)
                )
            ) {
                LauncherScreen(
                    gameDir = gameDir,
                    onOpenSafUri = { uri, onProgress ->
                        importUriStreaming(uri, gameDir, onProgress)
                    },
                    onLaunchGame = { resScale, fpsLimit, useTurnip, touchEnabled ->
                        writeConfigs(gameDir, resScale, fpsLimit, useTurnip, touchEnabled)
                        startActivity(Intent(this, SmashGameActivity::class.java))
                    }
                )
            }
        }
    }

    private suspend fun importUriStreaming(
        uri: Uri,
        gameDir: File,
        onProgress: (Int, String) -> Unit
    ): String = withContext(Dispatchers.IO) {
        val pfd = contentResolver.openFileDescriptor(uri, "r")
            ?: return@withContext "ERR|Tidak dapat membuka file dari SAF."
        val fd = pfd.detachFd()
        NativeBridge.importAndVerifyGameFile(
            fd = fd,
            outputDir = gameDir.absolutePath,
            callback = NativeBridge.ProgressCallback { pct, msg ->
                runOnUiThread {
                    onProgress(pct, msg)
                }
            }
        )
    }

    private fun writeConfigs(
        gameDir: File,
        resScale: Int,
        fpsLimit: Int,
        useTurnip: Boolean,
        touchEnabled: Boolean
    ) {
        val argsFile = File(gameDir, "args.txt")
        argsFile.writeText(
            buildString {
                appendLine("resolution_scale=$resScale")
                appendLine("fps_limit=$fpsLimit")
                appendLine("vulkan_driver=${if (useTurnip) "turnip" else "system"}")
                appendLine("touch_controls=${if (touchEnabled) 1 else 0}")
            }
        )

        val json = JSONObject().apply {
            put("game_id", "NALE")
            put("resolution_scale", resScale)
            put("fps_limit", fpsLimit)
            put("vulkan_driver", if (useTurnip) "turnip" else "system")
            put("touch_controls", touchEnabled)
        }
        File(gameDir, "config.json").writeText(json.toString(2))
    }
}

@Composable
private fun LauncherScreen(
    gameDir: File,
    onOpenSafUri: suspend (Uri, (Int, String) -> Unit) -> String,
    onLaunchGame: (Int, Int, Boolean, Boolean) -> Unit
) {
    val scope = rememberCoroutineScope()
    val romFile = remember(gameDir) { File(gameDir, "rom.z64") }

    var isReady by remember { mutableStateOf(romFile.exists() && romFile.length() > 0) }
    var isImporting by remember { mutableStateOf(false) }
    var progressPct by remember { mutableIntStateOf(0) }
    var statusMessage by remember {
        mutableStateOf(
            if (isReady) "ROM terverifikasi di storage privat (${romFile.length() / (1024 * 1024)} MB)."
            else "Pilih file Super Smash Bros. (USA) .z64 / .n64 / .v64 / .zip / .chd / .iso."
        )
    }
    var verifiedSha by remember { mutableStateOf("") }
    var errorMessage by remember { mutableStateOf<String?>(null) }

    var resScale by remember { mutableIntStateOf(1) }
    var fpsLimit by remember { mutableIntStateOf(60) }
    var useTurnip by remember { mutableStateOf(false) }
    var touchEnabled by remember { mutableStateOf(true) }

    val safLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        if (uri == null) return@rememberLauncherForActivityResult
        isImporting = true
        errorMessage = null
        progressPct = 0
        statusMessage = "Membuka stream SAF..."
        scope.launch {
            val result = onOpenSafUri(uri) { pct, msg ->
                progressPct = pct
                statusMessage = msg
            }
            isImporting = false
            if (result.startsWith("OK|")) {
                val parts = result.split("|")
                val sha = parts.getOrNull(1).orEmpty()
                val gameId = parts.getOrNull(2).orEmpty()
                val title = parts.getOrNull(3).orEmpty()
                verifiedSha = sha
                isReady = true
                val matchNote = if (sha.equals(ExpectedSha256, ignoreCase = true)) {
                    "SHA-256 cocok 100% (NALE USA)"
                } else {
                    "ROM terdeteksi: $title ($gameId) — hash berbeda dari NALE Rev0"
                }
                statusMessage = matchNote
            } else {
                errorMessage = result.removePrefix("ERR|")
                statusMessage = "Import gagal."
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFF0D0F14))
            .verticalScroll(rememberScrollState())
            .padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(
                    text = "SMASH 64 RECOMP",
                    color = Color(0xFFF2F4F8),
                    fontSize = 24.sp,
                    fontWeight = FontWeight.Bold,
                    letterSpacing = 1.sp
                )
                Text(
                    text = "Native ARM64 Static Recompilation • NALE (USA) • Adreno 610 Optimized",
                    color = Color(0xFF9AA4B2),
                    fontSize = 13.sp
                )
            }
            Surface(
                shape = RoundedCornerShape(8.dp),
                color = if (isReady) Color(0xFF143326) else Color(0xFF332018),
                border = BorderStroke(
                    1.dp,
                    if (isReady) Color(0xFF2E7D5B) else Color(0xFFB85D3B)
                )
            ) {
                Text(
                    text = if (isReady) "READY TO PLAY" else "NEEDS GAME FILES",
                    color = if (isReady) Color(0xFF69F0AE) else Color(0xFFFFAB91),
                    fontSize = 12.sp,
                    fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp)
                )
            }
        }

        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(12.dp),
            color = Color(0xFF161A23),
            border = BorderStroke(1.dp, Color(0xFF262C3A))
        ) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Text(
                    text = "BYOF Asset Manager (Android SAF)",
                    color = Color(0xFFF2F4F8),
                    fontWeight = FontWeight.SemiBold,
                    fontSize = 16.sp
                )
                Text(
                    text = statusMessage,
                    color = Color(0xFFB8C0CC),
                    fontSize = 13.sp
                )
                if (verifiedSha.isNotEmpty()) {
                    Text(
                        text = "SHA-256: $verifiedSha",
                        color = Color(0xFF81D4FA),
                        fontFamily = FontFamily.Monospace,
                        fontSize = 11.sp
                    )
                }
                if (isImporting) {
                    LinearProgressIndicator(
                        progress = { progressPct / 100f },
                        modifier = Modifier.fillMaxWidth(),
                        color = Color(0xFF29B6F6),
                        trackColor = Color(0xFF262C3A)
                    )
                }
                if (errorMessage != null) {
                    Text(
                        text = errorMessage!!,
                        color = Color(0xFFFF6E6E),
                        fontSize = 13.sp
                    )
                }

                Row(
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    OutlinedButton(
                        enabled = !isImporting,
                        onClick = { safLauncher.launch(arrayOf("*/*")) },
                        border = BorderStroke(1.dp, Color(0xFF3A4356))
                    ) {
                        Text(
                            text = if (isReady) "Re-Import ROM (.z64/.zip/.chd/.iso)"
                            else "Import Game File (.z64/.zip/.chd/.iso)",
                            color = Color(0xFFF2F4F8)
                        )
                    }

                    Button(
                        enabled = isReady && !isImporting,
                        onClick = { onLaunchGame(resScale, fpsLimit, useTurnip, touchEnabled) },
                        colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFE53935))
                    ) {
                        Text("Play Now", fontWeight = FontWeight.Bold, color = Color.White)
                    }
                }
            }
        }

        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(12.dp),
            color = Color(0xFF161A23),
            border = BorderStroke(1.dp, Color(0xFF262C3A))
        ) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Text(
                    text = "Quick Runtime Settings",
                    color = Color(0xFFF2F4F8),
                    fontWeight = FontWeight.SemiBold,
                    fontSize = 16.sp
                )

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text("Render Resolution", color = Color(0xFFB8C0CC), fontSize = 14.sp)
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        listOf(1, 2, 3, 4).forEach { scale ->
                            FilterChip(
                                selected = resScale == scale,
                                onClick = { resScale = scale },
                                label = { Text("${scale}x") },
                                colors = FilterChipDefaults.filterChipColors(
                                    selectedContainerColor = Color(0xFF29B6F6),
                                    selectedLabelColor = Color(0xFF0D0F14)
                                )
                            )
                        }
                    }
                }

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text("Frame Rate Limit", color = Color(0xFFB8C0CC), fontSize = 14.sp)
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        listOf(60 to "60 FPS", 120 to "120 FPS", 0 to "Max").forEach { (fps, label) ->
                            FilterChip(
                                selected = fpsLimit == fps,
                                onClick = { fpsLimit = fps },
                                label = { Text(label) },
                                colors = FilterChipDefaults.filterChipColors(
                                    selectedContainerColor = Color(0xFF29B6F6),
                                    selectedLabelColor = Color(0xFF0D0F14)
                                )
                            )
                        }
                    }
                }

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text("Vulkan Driver: Mesa Turnip (Adreno)", color = Color(0xFFB8C0CC), fontSize = 14.sp)
                        Text("Otomatis fallback ke System Vulkan jika tidak tersedia", color = Color(0xFF7A8494), fontSize = 12.sp)
                    }
                    Switch(checked = useTurnip, onCheckedChange = { useTurnip = it })
                }

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text("On-Screen Multi-Touch Controls", color = Color(0xFFB8C0CC), fontSize = 14.sp)
                        Text("Otomatis tersembunyi saat gamepad fisik terhubung", color = Color(0xFF7A8494), fontSize = 12.sp)
                    }
                    Switch(checked = touchEnabled, onCheckedChange = { touchEnabled = it })
                }
            }
        }

        Spacer(modifier = Modifier.height(8.dp))
    }
}

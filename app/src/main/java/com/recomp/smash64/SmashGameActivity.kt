package com.recomp.smash64

import org.libsdl.app.SDLActivity
import java.io.File

class SmashGameActivity : SDLActivity() {
    override fun getLibraries(): Array<String> {
        return arrayOf("SDL3", "game_recomp")
    }

    override fun getArguments(): Array<String> {
        val gameDir = File(filesDir, "games/nalp").absolutePath
        return arrayOf(gameDir)
    }
}

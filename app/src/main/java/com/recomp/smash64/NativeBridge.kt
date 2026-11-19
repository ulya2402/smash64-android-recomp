package com.recomp.smash64

object NativeBridge {
    init {
        System.loadLibrary("launcher_jni")
    }

    fun interface ProgressCallback {
        fun onProgress(percent: Int, message: String)
    }

    external fun importAndVerifyGameFile(
        fd: Int,
        outputDir: String,
        callback: ProgressCallback?
    ): String
}

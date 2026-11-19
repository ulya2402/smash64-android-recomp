#include <android/log.h>
#include <jni.h>
#include <libchdr/chd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "sha256.h"

#define LOG_TAG "Smash64LauncherJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

enum class N64Endian {
    Unknown,
    Z64_BigEndian,
    V64_ByteSwapped,
    N64_LittleEndian,
};

N64Endian detectN64Endian(const uint8_t hdr[4]) {
    if (hdr[0] == 0x80 && hdr[1] == 0x37 && hdr[2] == 0x12 && hdr[3] == 0x40) {
        return N64Endian::Z64_BigEndian;
    }
    if (hdr[0] == 0x37 && hdr[1] == 0x80 && hdr[2] == 0x40 && hdr[3] == 0x12) {
        return N64Endian::V64_ByteSwapped;
    }
    if (hdr[0] == 0x40 && hdr[1] == 0x12 && hdr[2] == 0x37 && hdr[3] == 0x80) {
        return N64Endian::N64_LittleEndian;
    }
    return N64Endian::Unknown;
}

void normalizeN64Chunk(uint8_t* buf, size_t len, N64Endian endian) {
    if (endian == N64Endian::V64_ByteSwapped) {
        for (size_t i = 0; i + 1 < len; i += 2) {
            std::swap(buf[i], buf[i + 1]);
        }
    } else if (endian == N64Endian::N64_LittleEndian) {
        for (size_t i = 0; i + 3 < len; i += 4) {
            std::swap(buf[i], buf[i + 3]);
            std::swap(buf[i + 1], buf[i + 2]);
        }
    }
}

void reportProgress(JNIEnv* env, jobject callback, jmethodID method, int percent, const char* status) {
    if (!callback || !method) return;
    jstring jstatus = env->NewStringUTF(status);
    env->CallVoidMethod(callback, method, static_cast<jint>(percent), jstatus);
    env->DeleteLocalRef(jstatus);
}

bool readExact(FILE* f, void* dst, size_t bytes) {
    return std::fread(dst, 1, bytes, f) == bytes;
}

uint16_t readLe16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t readLe32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool extractFirstN64FromZip(FILE* inFile, const std::string& outPath, JNIEnv* env, jobject cb,
                            jmethodID cbMethod, std::string& outErr) {
    std::rewind(inFile);
    uint8_t lfh[30]{};
    while (readExact(inFile, lfh, 30)) {
        if (readLe32(lfh) != 0x04034b50u) {
            outErr = "Arsip ZIP tidak valid atau menggunakan fitur terenkripsi.";
            return false;
        }
        uint16_t method = readLe16(lfh + 8);
        uint32_t compSize = readLe32(lfh + 18);
        uint32_t uncompSize = readLe32(lfh + 22);
        uint16_t nameLen = readLe16(lfh + 26);
        uint16_t extraLen = readLe16(lfh + 28);

        std::string filename(nameLen, '\0');
        if (nameLen > 0 && !readExact(inFile, filename.data(), nameLen)) {
            outErr = "Gagal membaca nama file di dalam ZIP.";
            return false;
        }
        if (extraLen > 0) {
            std::fseek(inFile, extraLen, SEEK_CUR);
        }

        std::string lower = filename;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        bool isN64 = lower.ends_with(".z64") || lower.ends_with(".n64") || lower.ends_with(".v64");

        if (!isN64) {
            std::fseek(inFile, static_cast<long>(compSize), SEEK_CUR);
            continue;
        }

        FILE* outFile = std::fopen(outPath.c_str(), "wb");
        if (!outFile) {
            outErr = "Gagal membuka output file untuk ekstraksi ZIP.";
            return false;
        }

        if (method == 0) {
            // Stored (uncompressed)
            std::vector<uint8_t> buf(256 * 1024);
            uint32_t remaining = compSize;
            while (remaining > 0) {
                size_t toRead = std::min<size_t>(buf.size(), remaining);
                if (!readExact(inFile, buf.data(), toRead)) {
                    std::fclose(outFile);
                    outErr = "File ZIP terpotong saat membaca ROM.";
                    return false;
                }
                std::fwrite(buf.data(), 1, toRead, outFile);
                remaining -= static_cast<uint32_t>(toRead);
                int pct = compSize > 0 ? static_cast<int>(50ULL * (compSize - remaining) / compSize) : 25;
                reportProgress(env, cb, cbMethod, pct, "Mengekstrak ROM dari ZIP...");
            }
            std::fclose(outFile);
            return true;
        } else if (method == 8) {
            // Deflate via zlib
            z_stream zs{};
            if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
                std::fclose(outFile);
                outErr = "Gagal inisialisasi zlib inflate.";
                return false;
            }
            std::vector<uint8_t> inBuf(128 * 1024);
            std::vector<uint8_t> outBuf(256 * 1024);
            uint32_t remaining = compSize;
            uint64_t totalWritten = 0;
            int ret = Z_OK;

            while (remaining > 0 && ret != Z_STREAM_END) {
                size_t toRead = std::min<size_t>(inBuf.size(), remaining);
                if (!readExact(inFile, inBuf.data(), toRead)) {
                    inflateEnd(&zs);
                    std::fclose(outFile);
                    outErr = "Gagal membaca stream deflate ZIP.";
                    return false;
                }
                remaining -= static_cast<uint32_t>(toRead);
                zs.next_in = inBuf.data();
                zs.avail_in = static_cast<uInt>(toRead);

                do {
                    zs.next_out = outBuf.data();
                    zs.avail_out = static_cast<uInt>(outBuf.size());
                    ret = inflate(&zs, Z_NO_FLUSH);
                    if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
                        inflateEnd(&zs);
                        std::fclose(outFile);
                        outErr = "Data ZIP korup saat dekompresi.";
                        return false;
                    }
                    size_t have = outBuf.size() - zs.avail_out;
                    if (have > 0) {
                        std::fwrite(outBuf.data(), 1, have, outFile);
                        totalWritten += have;
                    }
                } while (zs.avail_out == 0);

                int pct = uncompSize > 0 ? static_cast<int>(50ULL * totalWritten / uncompSize) : 30;
                reportProgress(env, cb, cbMethod, std::min(pct, 50), "Mendekompresi ROM dari ZIP...");
            }

            inflateEnd(&zs);
            std::fclose(outFile);
            return true;
        } else {
            std::fclose(outFile);
            outErr = "Metode kompresi ZIP tidak didukung (gunakan Store atau Deflate).";
            return false;
        }
    }

    outErr = "Tidak ditemukan file .z64 / .n64 / .v64 di dalam arsip ZIP.";
    return false;
}

bool extractFromChd(FILE* inFile, const std::string& outPath, JNIEnv* env, jobject cb,
                    jmethodID cbMethod, std::string& outErr) {
    std::rewind(inFile);
    chd_file* chd = nullptr;
    chd_error err = chd_open_file(inFile, CHD_OPEN_READ, nullptr, &chd);
    if (err != CHDERR_NONE || !chd) {
        outErr = std::string("Gagal membuka CHD: ") + chd_error_string(err);
        return false;
    }

    const chd_header* hdr = chd_get_header(chd);
    std::vector<uint8_t> hunkBuf(hdr->hunkbytes);
    FILE* outFile = std::fopen(outPath.c_str(), "wb");
    if (!outFile) {
        chd_close(chd);
        outErr = "Gagal membuat file output dari CHD.";
        return false;
    }

    for (uint32_t h = 0; h < hdr->totalhunks; ++h) {
        err = chd_read(chd, h, hunkBuf.data());
        if (err != CHDERR_NONE) {
            std::fclose(outFile);
            chd_close(chd);
            outErr = std::string("Error membaca hunk CHD: ") + chd_error_string(err);
            return false;
        }
        std::fwrite(hunkBuf.data(), 1, hdr->hunkbytes, outFile);
        if ((h & 15u) == 0) {
            int pct = static_cast<int>(50ULL * (h + 1) / std::max(1u, hdr->totalhunks));
            reportProgress(env, cb, cbMethod, pct, "Mendekompresi CHD via libchdr...");
        }
    }

    std::fclose(outFile);
    chd_close(chd);
    return true;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_recomp_smash64_NativeBridge_importAndVerifyGameFile(
    JNIEnv* env,
    jobject /* thiz */,
    jint fd,
    jstring jOutputDir,
    jobject progressCallback) {
    const char* outDirCStr = env->GetStringUTFChars(jOutputDir, nullptr);
    std::string outputDir(outDirCStr);
    env->ReleaseStringUTFChars(jOutputDir, outDirCStr);

    jmethodID cbMethod = nullptr;
    if (progressCallback != nullptr) {
        jclass cbClass = env->GetObjectClass(progressCallback);
        cbMethod = env->GetMethodID(cbClass, "onProgress", "(ILjava/lang/String;)V");
        env->DeleteLocalRef(cbClass);
    }

    FILE* rawFile = fdopen(fd, "rb");
    if (!rawFile) {
        close(fd);
        return env->NewStringUTF("ERR|Gagal membuka File Descriptor SAF.");
    }

    struct stat st {};
    fstat(fileno(rawFile), &st);
    const uint64_t totalSize = st.st_size > 0 ? static_cast<uint64_t>(st.st_size) : 0;

    uint8_t magic[16]{};
    if (std::fread(magic, 1, sizeof(magic), rawFile) < 4) {
        std::fclose(rawFile);
        return env->NewStringUTF("ERR|File terlalu kecil atau kosong.");
    }
    std::rewind(rawFile);

    const std::string tempRawPath = outputDir + "/import_tmp.bin";
    const std::string finalRomPath = outputDir + "/rom.z64";
    FILE* sourceFile = rawFile;
    bool closeSourceSeparately = false;

    // Deteksi format berdasarkan magic bytes
    if (magic[0] == 'P' && magic[1] == 'K' && magic[2] == 0x03 && magic[3] == 0x04) {
        reportProgress(env, progressCallback, cbMethod, 5, "Terdeteksi arsip ZIP...");
        std::string err;
        if (!extractFirstN64FromZip(rawFile, tempRawPath, env, progressCallback, cbMethod, err)) {
            std::fclose(rawFile);
            return env->NewStringUTF(("ERR|" + err).c_str());
        }
        std::fclose(rawFile);
        sourceFile = std::fopen(tempRawPath.c_str(), "rb");
        closeSourceSeparately = true;
    } else if (std::memcmp(magic, "MComprHD", 8) == 0) {
        reportProgress(env, progressCallback, cbMethod, 5, "Terdeteksi arsip CHD...");
        std::string err;
        if (!extractFromChd(rawFile, tempRawPath, env, progressCallback, cbMethod, err)) {
            std::fclose(rawFile);
            return env->NewStringUTF(("ERR|" + err).c_str());
        }
        std::fclose(rawFile);
        sourceFile = std::fopen(tempRawPath.c_str(), "rb");
        closeSourceSeparately = true;
    }

    if (!sourceFile) {
        return env->NewStringUTF("ERR|Gagal membuka stream ROM hasil ekstraksi.");
    }

    uint8_t n64Header[64]{};
    if (std::fread(n64Header, 1, sizeof(n64Header), sourceFile) != sizeof(n64Header)) {
        std::fclose(sourceFile);
        std::remove(tempRawPath.c_str());
        return env->NewStringUTF("ERR|Header N64 tidak valid (< 64 byte).");
    }

    N64Endian endian = detectN64Endian(n64Header);
    if (endian == N64Endian::Unknown) {
        std::fclose(sourceFile);
        std::remove(tempRawPath.c_str());
        return env->NewStringUTF("ERR|Format bukan ROM N64 (.z64/.n64/.v64) yang valid.");
    }

    std::rewind(sourceFile);
    FILE* outRom = std::fopen(finalRomPath.c_str(), "wb");
    if (!outRom) {
        std::fclose(sourceFile);
        std::remove(tempRawPath.c_str());
        return env->NewStringUTF("ERR|Gagal menulis rom.z64 ke storage privat aplikasi.");
    }

    smash64::crypto::Sha256 sha;
    std::vector<uint8_t> chunk(256 * 1024);
    uint64_t processed = 0;
    bool capturedHeader = false;
    uint8_t normalizedHeader[64]{};

    while (true) {
        size_t n = std::fread(chunk.data(), 1, chunk.size(), sourceFile);
        if (n == 0) break;

        normalizeN64Chunk(chunk.data(), n, endian);
        if (!capturedHeader && n >= 64) {
            std::memcpy(normalizedHeader, chunk.data(), 64);
            capturedHeader = true;
        }
        std::fwrite(chunk.data(), 1, n, outRom);
        sha.update(chunk.data(), n);
        processed += n;

        if (totalSize > 0) {
            int pct = 50 + static_cast<int>(50ULL * std::min(processed, totalSize) / totalSize);
            reportProgress(env, progressCallback, cbMethod, std::min(pct, 99),
                           "Menormalisasi .z64 & menghitung SHA-256...");
        }
    }

    std::fclose(outRom);
    std::fclose(sourceFile);
    if (closeSourceSeparately) {
        std::remove(tempRawPath.c_str());
    }

    const std::string shaHex = sha.finalHex();
    std::string title(reinterpret_cast<const char*>(normalizedHeader + 32), 20);
    while (!title.empty() && (title.back() == ' ' || title.back() == '\0')) {
        title.pop_back();
    }
    std::string gameId(reinterpret_cast<const char*>(normalizedHeader + 59), 4);

    reportProgress(env, progressCallback, cbMethod, 100, "Verifikasi selesai.");
    LOGI("Imported N64 ROM: id=%s title=%s size=%llu sha256=%s",
         gameId.c_str(), title.c_str(), static_cast<unsigned long long>(processed), shaHex.c_str());

    std::string result = "OK|" + shaHex + "|" + gameId + "|" + title + "|" + std::to_string(processed);
    return env->NewStringUTF(result.c_str());
}

#pragma once
// Minimal WAV I/O for tests and the render tool: 32-bit float write, PCM16/24/float32 read.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace acid::test
{
/** Writes interleaved 32-bit float samples. Returns false if the file cannot be written. */
inline bool writeWav (const std::string& path, const std::vector<float>& interleaved,
                      int numChannels, double sampleRate)
{
    std::FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr) return false;

    const auto dataBytes = static_cast<std::uint32_t> (interleaved.size() * sizeof (float));
    const auto put32 = [f] (std::uint32_t v) { std::fwrite (&v, 4, 1, f); };
    const auto put16 = [f] (std::uint16_t v) { std::fwrite (&v, 2, 1, f); };

    std::fwrite ("RIFF", 1, 4, f); put32 (36 + dataBytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); put32 (16);
    put16 (3);                                              // IEEE float
    put16 (static_cast<std::uint16_t> (numChannels));
    put32 (static_cast<std::uint32_t> (sampleRate));
    put32 (static_cast<std::uint32_t> (sampleRate) * static_cast<std::uint32_t> (numChannels) * 4u);
    put16 (static_cast<std::uint16_t> (numChannels * 4));
    put16 (32);
    std::fwrite ("data", 1, 4, f); put32 (dataBytes);
    std::fwrite (interleaved.data(), sizeof (float), interleaved.size(), f);
    return std::fclose (f) == 0;
}

struct WavData
{
    std::vector<float> interleaved;
    int numChannels = 0;
    double sampleRate = 0.0;
};

/** Reads PCM16, PCM24 or float32 WAV. Returns false on any unsupported or malformed file. */
inline bool readWav (const std::string& path, WavData& out)
{
    std::FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr) return false;

    char id[4];
    std::uint32_t size = 0;
    std::uint16_t format = 0, channels = 0, bits = 0;
    std::uint32_t rate = 0;
    bool haveFmt = false, ok = false;

    if (std::fread (id, 1, 4, f) == 4 && std::memcmp (id, "RIFF", 4) == 0
        && std::fseek (f, 4, SEEK_CUR) == 0
        && std::fread (id, 1, 4, f) == 4 && std::memcmp (id, "WAVE", 4) == 0)
    {
        while (std::fread (id, 1, 4, f) == 4 && std::fread (&size, 4, 1, f) == 1)
        {
            if (std::memcmp (id, "fmt ", 4) == 0)
            {
                std::uint8_t fmt[16] = {};
                if (size < 16 || std::fread (fmt, 1, 16, f) != 16) break;
                std::memcpy (&format, fmt, 2);  std::memcpy (&channels, fmt + 2, 2);
                std::memcpy (&rate, fmt + 4, 4); std::memcpy (&bits, fmt + 14, 2);
                std::fseek (f, static_cast<long> (size - 16 + (size & 1u)), SEEK_CUR);
                haveFmt = true;
            }
            else if (std::memcmp (id, "data", 4) == 0 && haveFmt && channels > 0)
            {
                const std::size_t bytesPer = bits / 8u;
                if (bytesPer == 0) break;
                const std::size_t count = size / bytesPer;
                std::vector<std::uint8_t> raw (size);
                if (std::fread (raw.data(), 1, size, f) != size) break;
                out.interleaved.resize (count);
                for (std::size_t i = 0; i < count; ++i)
                {
                    const std::uint8_t* p = raw.data() + i * bytesPer;
                    if (format == 3 && bits == 32)      { float v; std::memcpy (&v, p, 4); out.interleaved[i] = v; }
                    else if (format == 1 && bits == 16) { std::int16_t v; std::memcpy (&v, p, 2); out.interleaved[i] = static_cast<float> (v) / 32768.0f; }
                    else if (format == 1 && bits == 24) { const std::int32_t v = (p[0] | (p[1] << 8) | (static_cast<std::int8_t> (p[2]) << 16)); out.interleaved[i] = static_cast<float> (v) / 8388608.0f; }
                    else { std::fclose (f); return false; }
                }
                out.numChannels = channels;
                out.sampleRate = rate;
                ok = true;
                break;
            }
            else
            {
                std::fseek (f, static_cast<long> (size + (size & 1u)), SEEK_CUR);
            }
        }
    }
    std::fclose (f);
    return ok;
}
}

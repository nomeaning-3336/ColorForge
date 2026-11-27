#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>

namespace cf {

/** Writes the most compatible WAV there is: a 44-byte canonical header ('RIFF', 'WAVE', a 16-byte
    'fmt ' chunk with WAVE_FORMAT_PCM, then 'data'), stereo, 16 or 24-bit integer samples.

    JUCE's WavAudioFormat writes WAVE_FORMAT_EXTENSIBLE plus a JUNK padding chunk for any stereo
    file. That is valid, but some upload services (Suno among them) reject it as an
    "unsupported file format", so exports go through this writer instead. 16-bit gets TPDF dither. */
class PcmWavWriter
{
public:
    ~PcmWavWriter() { finish(); }

    juce::Result open (const juce::File& file, int sampleRate, int bitsPerSample)
    {
        jassert (bitsPerSample == 16 || bitsPerSample == 24);
        bits = bitsPerSample;
        rate = sampleRate;
        frames = 0;

        file.deleteFile();
        stream = file.createOutputStream();

        if (stream == nullptr || stream->failedToOpen())
        {
            stream.reset();
            return juce::Result::fail ("Can't write " + file.getFileName());
        }

        writeHeader();   // placeholder sizes, patched in finish()
        return juce::Result::ok();
    }

    /** Appends `num` stereo frames (channel 1 is reused if the buffer is mono). */
    bool write (const juce::AudioBuffer<float>& buffer, int start, int num)
    {
        if (stream == nullptr)
            return false;

        const int bytesPerSample = bits / 8;

        if ((frames + num) * 2 * bytesPerSample > (juce::int64) 0xffffffffLL - 64)
            return false;   // a plain RIFF file can't hold more than 4 GB

        const float* l = buffer.getReadPointer (0, start);
        const float* r = buffer.getReadPointer (juce::jmin (1, buffer.getNumChannels() - 1), start);
        const double scale = bits == 16 ? 32767.0 : 8388607.0;
        const int lo = bits == 16 ? -32768 : -8388608, hi = bits == 16 ? 32767 : 8388607;

        scratch.resize ((size_t) (num * 2 * bytesPerSample));
        auto* out = scratch.data();

        for (int i = 0; i < num; ++i)
        {
            for (const float* ch : { l, r })
            {
                double v = (double) ch[i] * scale;

                if (bits == 16)
                    v += dither (rng) + dither (rng);   // TPDF, +-1 LSB

                const int s = juce::jlimit (lo, hi, (int) std::lround (v));
                *out++ = (char) (s & 0xff);
                *out++ = (char) ((s >> 8) & 0xff);

                if (bits == 24)
                    *out++ = (char) ((s >> 16) & 0xff);
            }
        }

        frames += num;
        return stream->write (scratch.data(), scratch.size());
    }

    /** Patches the header sizes and closes the file. */
    bool finish()
    {
        if (stream == nullptr)
            return false;

        const auto dataBytes = dataSize();

        if ((dataBytes & 1) != 0)
            stream->writeByte (0);   // RIFF chunks are word aligned

        bool ok = stream->setPosition (0);
        writeHeader();
        stream->flush();
        ok = ok && stream->getStatus().wasOk();
        stream.reset();
        return ok;
    }

    int getSampleRate() const noexcept { return rate; }

private:
    juce::int64 dataSize() const noexcept { return frames * 2 * (bits / 8); }

    void writeHeader()
    {
        const auto dataBytes = (juce::uint32) dataSize();
        const int blockAlign = 2 * bits / 8;

        stream->write ("RIFF", 4);
        stream->writeInt ((int) (36 + dataBytes + (dataBytes & 1)));
        stream->write ("WAVE", 4);
        stream->write ("fmt ", 4);
        stream->writeInt (16);
        stream->writeShort (1);                    // WAVE_FORMAT_PCM
        stream->writeShort (2);                    // stereo
        stream->writeInt (rate);
        stream->writeInt (rate * blockAlign);      // bytes per second
        stream->writeShort ((short) blockAlign);
        stream->writeShort ((short) bits);
        stream->write ("data", 4);
        stream->writeInt ((int) dataBytes);
    }

    std::unique_ptr<juce::FileOutputStream> stream;
    std::vector<char> scratch;
    std::mt19937 rng { 0x5eed };
    std::uniform_real_distribution<double> dither { -0.5, 0.5 };
    int bits = 24, rate = 48000;
    juce::int64 frames = 0;
};

/** The export rate: 44.1 and 48 kHz pass through, anything else goes to the nearest family
    (88.2/176.4/22.05 kHz -> 44.1 kHz, the rest -> 48 kHz). */
inline int exportRateFor (double sourceRate)
{
    const int sr = juce::roundToInt (sourceRate);

    if (sr == 44100 || sr == 48000)
        return sr;

    return sr % 11025 == 0 ? 44100 : 48000;
}

/** Streams stereo audio from one sample rate to another: an 8th-order Butterworth low-pass when
    going down (against aliasing), then windowed-sinc interpolation, with its latency removed. */
class StereoResampler
{
public:
    StereoResampler (double fromRate, double toRate)
        : ratio (fromRate / toRate),
          toSkip (juce::roundToInt (juce::WindowedSincInterpolator::getBaseLatency() / ratio))
    {
        if (ratio > 1.0)
        {
            static constexpr double qs[] = { 0.50980, 0.60134, 0.89998, 2.56292 };

            for (auto& ch : filters)
                for (int i = 0; i < 4; ++i)
                    ch[(size_t) i].setCoefficients (juce::IIRCoefficients::makeLowPass (fromRate, 0.45 * toRate, qs[i]));
        }
    }

    bool isNeeded() const noexcept { return std::abs (ratio - 1.0) > 1.0e-9; }

    /** Feeds `num` frames and appends whatever output is ready to `out` (2 channels, resized). */
    void process (const juce::AudioBuffer<float>& in, int start, int num, juce::AudioBuffer<float>& out, int& outFrames)
    {
        for (int c = 0; c < 2; ++c)
        {
            auto& p = pending[(size_t) c];
            const auto old = p.size();
            p.insert (p.end(), in.getReadPointer (juce::jmin (c, in.getNumChannels() - 1), start),
                      in.getReadPointer (juce::jmin (c, in.getNumChannels() - 1), start) + num);

            if (ratio > 1.0)
                for (auto& f : filters[(size_t) c])
                    f.processSamples (p.data() + old, num);
        }

        // Leave a few input samples in hand so the interpolator never reads past the end.
        const int available = (int) pending[0].size() - 4;
        const int produce = available > 0 ? (int) std::floor (available / ratio) : 0;
        outFrames = 0;

        if (produce <= 0)
            return;

        out.setSize (2, produce, false, false, true);
        int used = 0;

        for (int c = 0; c < 2; ++c)
            used = interp[(size_t) c].process (ratio, pending[(size_t) c].data(), out.getWritePointer (c), produce);

        for (auto& p : pending)
            p.erase (p.begin(), p.begin() + juce::jmin ((int) p.size(), used));

        // Drop the interpolator's start-up latency so the export lines up with the source.
        const int skip = juce::jmin (toSkip, produce);
        toSkip -= skip;

        if (skip > 0)
            for (int c = 0; c < 2; ++c)
                std::memmove (out.getWritePointer (c), out.getReadPointer (c, skip), sizeof (float) * (size_t) (produce - skip));

        outFrames = produce - skip;
    }

private:
    double ratio;
    int toSkip;
    std::array<std::vector<float>, 2> pending;
    std::array<juce::WindowedSincInterpolator, 2> interp;
    std::array<std::array<juce::IIRFilter, 4>, 2> filters;
};

} // namespace cf

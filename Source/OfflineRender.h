#pragma once

#include "PluginProcessor.h"
#include "Automation.h"
#include "WavExport.h"

#include <functional>

namespace cf {

/** Renders a file offline through a fresh ColorForge instance into a plain PCM stereo WAV
    (16 or 24-bit, 44.1 or 48 kHz: see PcmWavWriter and exportRateFor).
    `state` reuses the settings of a live instance (nullptr = defaults) and `automation`
    plays its lanes in file time (nullptr = none).

    The audio goes to "<output>.part" and is renamed to `output` only once it is complete, so a
    half-written export never sits under the real name. `progress` (optional) gets 0..1 and can
    return false to cancel; a cancelled render leaves nothing behind. */
inline juce::Result renderFile (juce::AudioFormatManager& formats, const juce::File& input, const juce::File& output,
                                const juce::MemoryBlock* state, const Automation* automation = nullptr,
                                int bitsPerSample = 24,
                                const std::function<bool (double)>& progress = {})
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (input));

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return juce::Result::fail ("Can't read " + input.getFileName());

    constexpr int block = 256;   // automation is applied once per block (about 5 ms)
    const double rate = reader->sampleRate;

    ColorForgeAudioProcessor processor;

    if (state != nullptr && state->getSize() > 0)
        processor.setStateInformation (state->getData(), (int) state->getSize());

    if (automation != nullptr)
        automation->apply (processor.apvts, 0.0, true);

    processor.setNonRealtime (true);
    processor.setRateAndBufferSizeDetails (rate, block);
    processor.prepareToPlay (rate, block);

    const int outRate = exportRateFor (rate);
    StereoResampler resampler (rate, outRate);
    juce::AudioBuffer<float> resampled;
    const auto partial = output.getSiblingFile (output.getFileName() + ".part");
    struct RemovePartial { const juce::File& f; ~RemovePartial() { f.deleteFile(); } } cleanup { partial };
    PcmWavWriter writer;   // declared after `cleanup`, so it closes the file before it is removed

    if (auto opened = writer.open (partial, outRate, bitsPerSample == 16 ? 16 : 24); opened.failed())
        return opened;

    const juce::int64 latency = processor.getLatencySamples();
    const juce::int64 length = reader->lengthInSamples;
    const juce::int64 total = length + latency + (juce::int64) (rate * 4.0);   // 4 s tail for reverb and resonators

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;

    for (juce::int64 pos = 0; pos < total; pos += block)
    {
        if (progress && (pos & 0x3fff) == 0 && ! progress ((double) pos / (double) total))
            return juce::Result::fail ("Export cancelled");

        buffer.clear();

        if (pos < length)
            reader->read (&buffer, 0, (int) juce::jmin ((juce::int64) block, length - pos), pos, true, true);

        if (automation != nullptr)
            automation->apply (processor.apvts, (double) juce::jmin (pos, length) / rate, true);

        midi.clear();
        processor.processBlock (buffer, midi);

        // Drop the plugin latency so the export lines up with the source file.
        const int skip = (int) juce::jlimit ((juce::int64) 0, (juce::int64) block, latency - pos);

        if (skip >= block)
            continue;

        bool ok = true;

        if (resampler.isNeeded())
        {
            int frames = 0;
            resampler.process (buffer, skip, block - skip, resampled, frames);
            ok = frames == 0 || writer.write (resampled, 0, frames);
        }
        else
        {
            ok = writer.write (buffer, skip, block - skip);
        }

        if (! ok)
            return juce::Result::fail ("Write error on " + output.getFileName());
    }

    if (! writer.finish())
        return juce::Result::fail ("Write error on " + output.getFileName());

    output.deleteFile();

    if (! partial.moveFileTo (output))
        return juce::Result::fail ("Can't write " + output.getFileName());

    if (progress)
        progress (1.0);

    return juce::Result::ok();
}

} // namespace cf

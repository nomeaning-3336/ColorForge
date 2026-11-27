#pragma once

#include "PluginProcessor.h"
#include "Automation.h"

namespace cf {

/** Renders a file offline through a fresh ColorForge instance into a 24-bit stereo WAV.
    `state` reuses the settings of a live instance (nullptr = defaults) and `automation`
    plays its lanes in file time (nullptr = none). */
inline juce::Result renderFile (juce::AudioFormatManager& formats, const juce::File& input, const juce::File& output,
                                const juce::MemoryBlock* state, const Automation* automation = nullptr)
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

    output.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (output.createOutputStream());

    if (stream == nullptr)
        return juce::Result::fail ("Can't write " + output.getFileName());

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), rate, 2, 24, {}, 0));

    if (writer == nullptr)
        return juce::Result::fail ("Can't create a WAV writer for " + output.getFileName());

    stream.release();   // the writer owns the stream now

    const juce::int64 latency = processor.getLatencySamples();
    const juce::int64 length = reader->lengthInSamples;
    const juce::int64 total = length + latency + (juce::int64) (rate * 4.0);   // 4 s tail for reverb and resonators

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;

    for (juce::int64 pos = 0; pos < total; pos += block)
    {
        buffer.clear();

        if (pos < length)
            reader->read (&buffer, 0, (int) juce::jmin ((juce::int64) block, length - pos), pos, true, true);

        if (automation != nullptr)
            automation->apply (processor.apvts, (double) juce::jmin (pos, length) / rate, true);

        midi.clear();
        processor.processBlock (buffer, midi);

        // Drop the plugin latency so the export lines up with the source file.
        const int skip = (int) juce::jlimit ((juce::int64) 0, (juce::int64) block, latency - pos);

        if (skip < block && ! writer->writeFromAudioSampleBuffer (buffer, skip, block - skip))
            return juce::Result::fail ("Write error on " + output.getFileName());
    }

    writer.reset();
    return juce::Result::ok();
}

} // namespace cf

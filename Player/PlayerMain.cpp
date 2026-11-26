// ColorForge Player: a standalone host for the ColorForge processor.
// Drop an audio file on the window and it loops through the effect, as if the
// sample sat on a track with the plugin inserted.

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::Colour kBackground = cfui::px::pal::bg;
const juce::Colour kPanel      = cfui::px::pal::panel;
const juce::Colour kText       = cfui::px::pal::text;

//==============================================================================
/** Plays an audio file through the ColorForge processor to the audio device. */
class PlayerEngine final : public juce::AudioIODeviceCallback,
                           public juce::MidiInputCallback
{
public:
    PlayerEngine()
    {
        formatManager.registerBasicFormats();
        readAhead.startThread();
        work.setSize (2, kMaxSlice);

        deviceError = deviceManager.initialiseWithDefaultDevices (0, 2);
        deviceManager.addAudioCallback (this);
        deviceManager.addMidiInputDeviceCallback ({}, this);
    }

    ~PlayerEngine() override
    {
        deviceManager.removeMidiInputDeviceCallback ({}, this);
        deviceManager.removeAudioCallback (this);
        deviceManager.closeAudioDevice();
        transport.setSource (nullptr);
        readerSource.reset();
        readAhead.stopThread (2000);
    }

    bool loadFile (const juce::File& file)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

        if (reader == nullptr || reader->lengthInSamples <= 0)
            return false;

        const double fileRate = reader->sampleRate;
        auto newSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
        newSource->setLooping (looping);

        transport.stop();
        transport.setSource (newSource.get(), 32768, &readAhead, fileRate, 2);
        readerSource = std::move (newSource);   // the previous source is released only after the transport let go of it
        transport.setPosition (0.0);
        transport.start();
        return true;
    }

    bool hasFile() const noexcept       { return readerSource != nullptr; }

    void setLooping (bool shouldLoop)
    {
        looping = shouldLoop;

        if (readerSource != nullptr)
            readerSource->setLooping (shouldLoop);
    }

    void togglePlay()
    {
        if (transport.isPlaying())
        {
            transport.stop();
            return;
        }

        if (readerSource == nullptr)
            return;

        if (transport.getCurrentPosition() >= transport.getLengthInSeconds() - 0.01)
            transport.setPosition (0.0);

        transport.start();
    }

    //==============================================================================
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        const double rate = device->getCurrentSampleRate();
        const int block = device->getCurrentBufferSizeSamples();

        const juce::ScopedLock sl (processor.getCallbackLock());
        processor.setRateAndBufferSizeDetails (rate, block);
        processor.prepareToPlay (rate, block);
        transport.prepareToPlay (block, rate);
        gainNow = inputGain.load();
    }

    void audioDeviceStopped() override
    {
        transport.releaseResources();

        const juce::ScopedLock sl (processor.getCallbackLock());
        processor.releaseResources();
    }

    void audioDeviceIOCallbackWithContext (const float* const*, int,
                                           float* const* output, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        juce::ScopedNoDenormals noDenormals;

        for (int pos = 0; pos < numSamples; pos += kMaxSlice)
        {
            const int n = juce::jmin ((int) kMaxSlice, numSamples - pos);
            juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), 2, n);

            transport.getNextAudioBlock (juce::AudioSourceChannelInfo (&view, 0, n));

            const float target = inputGain.load (std::memory_order_relaxed);
            view.applyGainRamp (0, n, gainNow, target);
            gainNow = target;

            midi.clear();
            keyboardState.processNextMidiBuffer (midi, 0, n, true);

            {
                const juce::ScopedLock sl (processor.getCallbackLock());
                processor.processBlock (view, midi);
            }

            if (numOutputs == 1 && output[0] != nullptr)
            {
                const float* l = view.getReadPointer (0);
                const float* r = view.getReadPointer (1);

                for (int i = 0; i < n; ++i)
                    output[0][pos + i] = 0.5f * (l[i] + r[i]);
            }
            else
            {
                for (int ch = 0; ch < numOutputs; ++ch)
                {
                    if (output[ch] == nullptr)
                        continue;

                    if (ch < 2)
                        juce::FloatVectorOperations::copy (output[ch] + pos, view.getReadPointer (ch), n);
                    else
                        juce::FloatVectorOperations::clear (output[ch] + pos, n);
                }
            }
        }
    }

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message) override
    {
        keyboardState.processNextMidiEvent (message);
    }

    //==============================================================================
    ColorForgeAudioProcessor processor;
    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;
    juce::AudioTransportSource transport;
    juce::MidiKeyboardState keyboardState;
    std::atomic<float> inputGain { 1.0f };
    juce::String deviceError;

private:
    static constexpr int kMaxSlice = 2048;

    juce::TimeSliceThread readAhead { "ColorForge read-ahead" };
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioBuffer<float> work;
    juce::MidiBuffer midi;
    float gainNow = 1.0f;
    bool looping = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlayerEngine)
};

//==============================================================================
/** Renders a file offline through a fresh ColorForge instance into a 24-bit stereo WAV.
    Pass the plugin state to reuse the settings of a live instance; nullptr = defaults. */
juce::Result renderFile (juce::AudioFormatManager& formats, const juce::File& input, const juce::File& output,
                         const juce::MemoryBlock* state)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (input));

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return juce::Result::fail ("Can't read " + input.getFileName());

    constexpr int block = 512;
    const double rate = reader->sampleRate;

    ColorForgeAudioProcessor processor;

    if (state != nullptr && state->getSize() > 0)
        processor.setStateInformation (state->getData(), (int) state->getSize());

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

//==============================================================================
/** Waveform overview with playhead. Click or drag to seek. */
class WaveformView final : public juce::Component,
                           private juce::ChangeListener,
                           private juce::Timer
{
public:
    explicit WaveformView (PlayerEngine& e)
        : engine (e), thumbnail (512, e.formatManager, cache)
    {
        thumbnail.addChangeListener (this);
        startTimerHz (30);
    }

    ~WaveformView() override
    {
        thumbnail.removeChangeListener (this);
    }

    void setFile (const juce::File& file)
    {
        thumbnail.setSource (new juce::FileInputSource (file));
        repaint();
    }

    void setDragHighlight (bool shouldHighlight)
    {
        if (highlight != shouldHighlight)
        {
            highlight = shouldHighlight;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced (1.0f);
        cfui::px::bevel (g, getLocalBounds().toFloat(), 2.0f, highlight ? cfui::px::pal::panelHi : cfui::px::pal::bgAlt, true);

        const double length = thumbnail.getTotalLength();

        if (length > 0.0)
        {
            juce::ColourGradient rainbow (juce::Colour::fromHSV (0.0f, 0.75f, 1.0f, 1.0f), area.getX(), 0.0f,
                                          juce::Colour::fromHSV (0.85f, 0.75f, 1.0f, 1.0f), area.getRight(), 0.0f, false);

            for (int i = 1; i < 8; ++i)
                rainbow.addColour ((double) i / 8.0, juce::Colour::fromHSV (0.85f * (float) i / 8.0f, 0.75f, 1.0f, 1.0f));

            g.setGradientFill (rainbow);
            thumbnail.drawChannels (g, getLocalBounds().reduced (4, 3), 0.0, length, 1.0f);

            const double playLength = engine.transport.getLengthInSeconds();

            if (playLength > 0.0)
            {
                const float x = area.getX() + (float) (engine.transport.getCurrentPosition() / playLength) * area.getWidth();
                g.setColour (juce::Colours::white);
                g.fillRect (x - 0.75f, area.getY() + 2.0f, 1.5f, area.getHeight() - 4.0f);
            }
        }
        else
        {
            cfui::px::drawTextIn (g, "DROP AN AUDIO FILE ANYWHERE IN THIS WINDOW  (WAV / AIFF / FLAC / OGG / MP3)",
                                  area, juce::Justification::centred, 2.0f,
                                  highlight ? cfui::px::pal::yellow : cfui::px::pal::dim);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override     { seek (e); }
    void mouseDrag (const juce::MouseEvent& e) override     { seek (e); }

private:
    void seek (const juce::MouseEvent& e)
    {
        const double length = engine.transport.getLengthInSeconds();

        if (length > 0.0 && getWidth() > 0)
            engine.transport.setPosition (juce::jlimit (0.0, length, length * (double) e.position.x / (double) getWidth()));
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override   { repaint(); }

    void timerCallback() override
    {
        if (thumbnail.getTotalLength() > 0.0)
            repaint();
    }

    PlayerEngine& engine;
    juce::AudioThumbnailCache cache { 4 };
    juce::AudioThumbnail thumbnail;
    bool highlight = false;
};

//==============================================================================
class PlayerComponent final : public juce::Component,
                              public juce::FileDragAndDropTarget,
                              private juce::Timer
{
public:
    PlayerComponent()
    {
        setOpaque (true);
        setLookAndFeel (&lnf);

        editor.reset (engine.processor.createEditor());
        addAndMakeVisible (*editor);
        addAndMakeVisible (waveform);

        addAndMakeVisible (openButton);
        openButton.onClick = [this] { openFile(); };

        addAndMakeVisible (playButton);
        playButton.onClick = [this] { engine.togglePlay(); };

        addAndMakeVisible (loopButton);
        loopButton.setToggleState (true, juce::dontSendNotification);
        loopButton.setColour (juce::ToggleButton::textColourId, kText);
        loopButton.onClick = [this] { engine.setLooping (loopButton.getToggleState()); };

        addAndMakeVisible (exportButton);
        exportButton.setEnabled (false);
        exportButton.onClick = [this] { exportFile(); };

        addAndMakeVisible (audioButton);
        audioButton.onClick = [this] { showAudioSettings(); };

        addAndMakeVisible (gainLabel);
        gainLabel.setText ("Input", juce::dontSendNotification);
        gainLabel.setColour (juce::Label::textColourId, kText);
        gainLabel.setJustificationType (juce::Justification::centredRight);

        addAndMakeVisible (gainSlider);
        gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
        gainSlider.setRange (-36.0, 12.0, 0.1);
        gainSlider.setValue (0.0, juce::dontSendNotification);
        gainSlider.setTextValueSuffix (" dB");
        gainSlider.setDoubleClickReturnValue (true, 0.0);
        gainSlider.onValueChange = [this]
        {
            engine.inputGain.store (juce::Decibels::decibelsToGain ((float) gainSlider.getValue()));
        };

        addAndMakeVisible (statusLabel);
        statusLabel.setColour (juce::Label::textColourId, kText);
        statusLabel.setText (engine.deviceError.isNotEmpty() ? "Audio device: " + engine.deviceError
                                                             : juce::String ("No file loaded. MIDI Follow: play the keys below."),
                             juce::dontSendNotification);

        addAndMakeVisible (keyboard);
        keyboard.setAvailableRange (36, 96);

        for (juce::Component* c : { (juce::Component*) &openButton, (juce::Component*) &playButton,
                                    (juce::Component*) &loopButton, (juce::Component*) &exportButton,
                                    (juce::Component*) &audioButton, (juce::Component*) &gainLabel,
                                    (juce::Component*) &statusLabel })
            c->getProperties().set ("pxScale", 1.5f);

        setWantsKeyboardFocus (true);
        setSize (editor->getWidth(), kTopBar + kWave + editor->getHeight() + kKeys);
        startTimerHz (10);
    }

    ~PlayerComponent() override
    {
        stopTimer();
        setLookAndFeel (nullptr);

        if (editor != nullptr)
        {
            engine.processor.editorBeingDeleted (editor.get());
            editor.reset();
        }
    }

    bool load (const juce::File& file)
    {
        if (! file.existsAsFile())
            return false;

        if (! engine.loadFile (file))
        {
            statusLabel.setText ("Can't read " + file.getFileName(), juce::dontSendNotification);
            return false;
        }

        currentFile = file;
        exportButton.setEnabled (true);
        waveform.setFile (file);
        statusLabel.setText (file.getFileName(), juce::dontSendNotification);
        return true;
    }

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        g.fillAll (kBackground);
    }

    void resized() override
    {
        auto area = getLocalBounds();

        auto top = area.removeFromTop (kTopBar).reduced (8, 6);
        openButton.setBounds (top.removeFromLeft (84));
        top.removeFromLeft (6);
        playButton.setBounds (top.removeFromLeft (84));
        top.removeFromLeft (10);
        loopButton.setBounds (top.removeFromLeft (80));
        top.removeFromLeft (6);
        exportButton.setBounds (top.removeFromLeft (124));
        audioButton.setBounds (top.removeFromRight (176));
        top.removeFromRight (10);
        gainSlider.setBounds (top.removeFromRight (250));
        gainLabel.setBounds (top.removeFromRight (64));
        statusLabel.setBounds (top.reduced (6, 0));

        waveform.setBounds (area.removeFromTop (kWave).reduced (8, 2));
        keyboard.setBounds (area.removeFromBottom (kKeys));
        keyboard.setKeyWidth ((float) getWidth() / 36.0f);   // 36 white keys in C2..C7

        if (editor != nullptr)
            editor->setTopLeftPosition (area.getX(), area.getY());
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::spaceKey)
        {
            engine.togglePlay();
            return true;
        }

        return false;
    }

    //==============================================================================
    bool isInterestedInFileDrag (const juce::StringArray&) override     { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override    { waveform.setDragHighlight (true); }
    void fileDragExit (const juce::StringArray&) override               { waveform.setDragHighlight (false); }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        waveform.setDragHighlight (false);

        for (const auto& path : files)
            if (load (juce::File (path)))
                break;
    }

private:
    static constexpr int kTopBar = 40, kWave = 64, kKeys = 60;

    void openFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Open an audio file", juce::File(),
                                                       engine.formatManager.getWildcardForAllFormats());

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();

                                  if (file.existsAsFile())
                                      load (file);
                              });
    }

    /** Renders the loaded file through the current plugin settings into a WAV. */
    void exportFile()
    {
        if (! currentFile.existsAsFile())
            return;

        chooser = std::make_unique<juce::FileChooser> ("Export processed audio",
                                                       currentFile.getSiblingFile (currentFile.getFileNameWithoutExtension()
                                                                                       + " (ColorForge).wav"),
                                                       "*.wav");

        chooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto target = fc.getResult();

                                  if (target == juce::File())
                                      return;

                                  target = target.withFileExtension ("wav");

                                  juce::MemoryBlock state;
                                  engine.processor.getStateInformation (state);

                                  juce::MouseCursor::showWaitCursor();
                                  const auto result = renderFile (engine.formatManager, currentFile, target, &state);
                                  juce::MouseCursor::hideWaitCursor();

                                  statusLabel.setText (result.wasOk() ? "Exported " + target.getFileName()
                                                                      : result.getErrorMessage(),
                                                       juce::dontSendNotification);
                              });
    }

    void showAudioSettings()
    {
        auto* selector = new juce::AudioDeviceSelectorComponent (engine.deviceManager, 0, 0, 2, 2,
                                                                 true, false, true, false);
        selector->setSize (520, 420);

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (selector);
        options.dialogTitle = "Audio Settings";
        options.dialogBackgroundColour = kPanel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.launchAsync();
    }

    void timerCallback() override
    {
        playButton.setButtonText (engine.transport.isPlaying() ? "Stop" : "Play");
        playButton.setEnabled (engine.hasFile());
    }

    PlayerEngine engine;
    WaveformView waveform { engine };
    cfui::PixelLookAndFeel lnf;
    juce::MidiKeyboardComponent keyboard { engine.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
    juce::TextButton openButton { "Open..." }, playButton { "Play" }, exportButton { "Export WAV" },
                     audioButton { "Audio Settings" };
    juce::ToggleButton loopButton { "Loop" };
    juce::Slider gainSlider;
    juce::Label gainLabel, statusLabel;
    juce::File currentFile;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AudioProcessorEditor> editor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlayerComponent)
};

//==============================================================================
class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow (const juce::String& commandLine)
        : DocumentWindow ("ColorForge Player", kBackground,
                          juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);

        auto* content = new PlayerComponent();
        setContentOwned (content, true);
        setResizable (false, false);

        // Shrink the whole UI on screens that are too small for it.
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto avail = display->userArea;
            const float scale = juce::jmin (1.0f,
                                            (float) (avail.getHeight() - 48) / (float) getHeight(),
                                            (float) (avail.getWidth() - 16) / (float) getWidth());

            if (scale < 0.999f)
                juce::Desktop::getInstance().setGlobalScaleFactor (juce::jmax (0.5f, scale));
        }

        centreWithSize (getWidth(), getHeight());
        setVisible (true);

        const auto arg = commandLine.unquoted().trim();

        if (arg.isNotEmpty())
            content->load (juce::File::getCurrentWorkingDirectory().getChildFile (arg));
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

//==============================================================================
class PlayerApplication final : public juce::JUCEApplication
{
public:
    PlayerApplication() = default;

    const juce::String getApplicationName() override        { return "ColorForge Player"; }
    const juce::String getApplicationVersion() override     { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override              { return true; }

    void initialise (const juce::String& commandLine) override
    {
        // Headless mode:  "ColorForge Player.exe" --render <input> <output.wav>
        const auto args = getCommandLineParameterArray();

        if (args.size() >= 3 && args[0] == "--render")
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();

            const auto cwd = juce::File::getCurrentWorkingDirectory();
            const auto result = renderFile (formats, cwd.getChildFile (args[1]), cwd.getChildFile (args[2]), nullptr);

            setApplicationReturnValue (result.wasOk() ? 0 : 1);
            quit();
            return;
        }

        window = std::make_unique<MainWindow> (commandLine);
    }

    void shutdown() override                                { window.reset(); }
    void systemRequestedQuit() override                     { quit(); }

private:
    std::unique_ptr<MainWindow> window;
};
} // namespace

START_JUCE_APPLICATION (PlayerApplication)

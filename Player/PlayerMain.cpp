// ColorForge Player: a standalone host for the ColorForge processor.
// Drop an audio file on the window and it loops through the effect, as if the
// sample sat on a track with the plugin inserted.

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "OfflineRender.h"

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

            // automation lanes play in file time, once per slice (about 5 ms)
            if (readLanes.load (std::memory_order_relaxed) && transport.isPlaying())
                automation.apply (processor.apvts, transport.getCurrentPosition());

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
    std::atomic<bool> readLanes { true };
    cf::Automation automation;
    juce::String deviceError;

private:
    static constexpr int kMaxSlice = 256;

    juce::TimeSliceThread readAhead { "ColorForge read-ahead" };
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioBuffer<float> work;
    juce::MidiBuffer midi;
    float gainNow = 1.0f;
    bool looping = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlayerEngine)
};

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
/** Touch-records knob moves into the automation lanes while the file plays: a lane is
    written from the moment a control is grabbed until it is let go, then playback of that
    lane resumes. Gestures come from the editor's attachments on the message thread. */
class AutomationRecorder final : private juce::AudioProcessorParameter::Listener
{
public:
    explicit AutomationRecorder (PlayerEngine& e) : engine (e)
    {
        for (auto* prm : engine.processor.getParameters())
            prm->addListener (this);
        touches.resize ((size_t) engine.processor.getParameters().size());
    }

    ~AutomationRecorder() override
    {
        for (auto* prm : engine.processor.getParameters())
            prm->removeListener (this);
    }

    bool armed = false;
    std::function<void (const juce::String&)> onLaneTouched;

    /** Called from the UI timer: extends every held control's lane up to now. */
    void tick()
    {
        for (size_t i = 0; i < touches.size(); ++i)
            if (touches[i].active)
                writeNow ((int) i);
    }

private:
    struct Touch { bool active = false; double lastT = 0.0; };

    juce::RangedAudioParameter* paramAt (int index) const
    {
        return dynamic_cast<juce::RangedAudioParameter*> (engine.processor.getParameters()[index]);
    }

    double now() const      { return engine.transport.getCurrentPosition(); }

    void writeNow (int index)
    {
        auto* prm = paramAt (index);
        auto& t = touches[(size_t) index];
        const double n = now();
        engine.automation.write (prm->getParameterID(), t.lastT, n, prm->getValue());
        t.lastT = n;
    }

    void parameterValueChanged (int, float) override {}   // also fires from lane playback; ignored

    void parameterGestureChanged (int index, bool starting) override
    {
        if (! juce::MessageManager::existsAndIsCurrentThread() || index < 0 || index >= (int) touches.size())
            return;
        auto* prm = paramAt (index);
        if (prm == nullptr)
            return;
        auto& t = touches[(size_t) index];
        const auto id = prm->getParameterID();

        if (starting)
        {
            if (! armed || ! engine.transport.isPlaying())
                return;
            t.active = true;
            t.lastT = now();
            engine.automation.setTouching (id, true);
            engine.automation.beginTouch (id, t.lastT, prm->getValue(), prm->isDiscrete() || prm->isBoolean());
            writeNow (index);
            if (onLaneTouched)
                onLaneTouched (id);
        }
        else if (t.active)
        {
            writeNow (index);
            t.active = false;
            engine.automation.setTouching (id, false);
        }
    }

    PlayerEngine& engine;
    std::vector<Touch> touches;
};

//==============================================================================
/** One automation lane over the file's timeline. Pick the lane on the left; drag on the
    curve to draw, double-click to clear the lane. */
class LaneView final : public juce::Component,
                       private juce::Timer
{
public:
    explicit LaneView (PlayerEngine& e) : engine (e)
    {
        laneBox.setTextWhenNothingSelected ("NO LANES YET");
        laneBox.setTooltip ("Automation lane to show and edit");
        laneBox.onChange = [this] { shownId = idForItem (laneBox.getSelectedId()); repaint(); };
        laneBox.getProperties().set ("pxScale", 1.5f);
        addAndMakeVisible (laneBox);
        startTimerHz (30);
    }

    void show (const juce::String& id)
    {
        shownId = id;
        refreshList();
    }

    void refreshList()
    {
        const auto ids = engine.automation.laneIds();
        if (ids == listed && laneBox.getSelectedId() == itemFor (shownId))
            return;
        listed = ids;
        laneBox.clear (juce::dontSendNotification);
        for (int i = 0; i < ids.size(); ++i)
            if (auto* prm = engine.processor.apvts.getParameter (ids[i]))
                laneBox.addItem (prm->getName (40).toUpperCase(), i + 1);
        if (! ids.contains (shownId))
            shownId = ids.isEmpty() ? juce::String() : ids[0];
        laneBox.setSelectedId (itemFor (shownId), juce::dontSendNotification);
        repaint();
    }

    juce::String getShownId() const     { return shownId; }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 4);
        auto left = r.removeFromLeft (220);
        laneBox.setBounds (left.removeFromTop (24));
        plot = r.withTrimmedLeft (8);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace cfui::px;
        drawTextIn (g, "AUTOMATION", getLocalBounds().toFloat().reduced (6.0f, 0.0f).withTrimmedTop (34.0f).withHeight (12.0f),
                    juce::Justification::centredLeft, 1.5f, pal::dim);
        drawTextIn (g, "DRAG = DRAW  \x04  DOUBLE-CLICK = CLEAR", getLocalBounds().toFloat().reduced (6.0f, 0.0f).withTrimmedTop (50.0f).withHeight (10.0f),
                    juce::Justification::centredLeft, 1.0f, pal::dim);

        auto area = plot.toFloat();
        bevel (g, area, 2.0f, pal::bgAlt, true);
        const double len = engine.transport.getLengthInSeconds();
        auto* prm = engine.processor.apvts.getParameter (shownId);

        if (len <= 0.0 || prm == nullptr)
        {
            drawTextIn (g, len <= 0.0 ? "LOAD A FILE, ARM REC, PRESS PLAY AND MOVE A KNOB"
                                      : "ARM REC AND MOVE A KNOB WHILE THE FILE PLAYS",
                        area, juce::Justification::centred, 1.5f, pal::dim);
            return;
        }

        auto inner = area.reduced (4.0f, 6.0f);
        const auto pts = engine.automation.copyLane (shownId);
        const bool discrete = engine.automation.isDiscrete (shownId);
        juce::RectangleList<float> line;
        const int cols = (int) (inner.getWidth() / 2.0f);
        float prevY = -1.0f;
        for (int c = 0; c < cols; ++c)
        {
            const double t = len * (double) c / (double) juce::jmax (1, cols - 1);
            const float v = engine.automation.valueAt (shownId, t);
            const float y = std::floor (juce::jmap (v, 0.0f, 1.0f, inner.getBottom() - 2.0f, inner.getY()) / 2.0f) * 2.0f;
            const float a = prevY < 0.0f ? y : juce::jmin (prevY, y), b = prevY < 0.0f ? y : juce::jmax (prevY, y);
            line.addWithoutMerging ({ inner.getX() + (float) c * 2.0f, a, 2.0f, b - a + 2.0f });
            prevY = y;
        }
        g.setColour (pal::yellow.withAlpha (0.9f));
        g.fillRectList (line);

        if (pts.size() < 400)
            for (const auto& p : pts)
            {
                const float x = inner.getX() + (float) (p.t / len) * inner.getWidth();
                const float y = juce::jmap (p.v, 0.0f, 1.0f, inner.getBottom(), inner.getY());
                g.setColour (pal::text);
                g.fillRect (std::round (x) - 1.0f, std::round (y) - 1.0f, 3.0f, 3.0f);
            }

        const float px = inner.getX() + (float) (engine.transport.getCurrentPosition() / len) * inner.getWidth();
        g.setColour (juce::Colours::white);
        g.fillRect (px - 0.75f, area.getY() + 2.0f, 1.5f, area.getHeight() - 4.0f);

        juce::String txt = prm->getName (40).toUpperCase() + "  " + prm->getCurrentValueAsText().toUpperCase();
        if (discrete)
            txt << "  (STEPS)";
        drawTextIn (g, txt, area.reduced (8.0f, 4.0f).withHeight (12.0f), juce::Justification::topLeft, 1.0f, pal::text);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! plot.contains (e.getPosition()) || engine.processor.apvts.getParameter (shownId) == nullptr)
            return;
        drawing = true;
        lastT = timeAt (e.position.x);
        engine.automation.setTouching (shownId, true);
        engine.automation.beginTouch (shownId, lastT, valueAt (e.position.y), engine.automation.isDiscrete (shownId));
        engine.automation.write (shownId, lastT, lastT, valueAt (e.position.y));
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! drawing)
            return;
        const double t = timeAt (e.position.x);
        const float v = valueAt (e.position.y);
        if (t >= lastT)
            engine.automation.write (shownId, lastT, t, v);
        else
            engine.automation.write (shownId, t - 1.0e-6, lastT, v);   // drawing right to left
        lastT = t;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (drawing)
            engine.automation.setTouching (shownId, false);
        drawing = false;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (plot.contains (e.getPosition()) && shownId.isNotEmpty())
        {
            engine.automation.clear (shownId);
            refreshList();
        }
    }

private:
    double timeAt (float x) const
    {
        const auto inner = plot.toFloat().reduced (4.0f, 6.0f);
        return juce::jlimit (0.0, engine.transport.getLengthInSeconds(),
                             (double) ((x - inner.getX()) / inner.getWidth()) * engine.transport.getLengthInSeconds());
    }

    float valueAt (float y) const
    {
        const auto inner = plot.toFloat().reduced (4.0f, 6.0f);
        return juce::jlimit (0.0f, 1.0f, juce::jmap (y, inner.getBottom(), inner.getY(), 0.0f, 1.0f));
    }

    int itemFor (const juce::String& id) const    { const int i = listed.indexOf (id); return i < 0 ? 0 : i + 1; }
    juce::String idForItem (int item) const       { return item > 0 && item <= listed.size() ? listed[item - 1] : juce::String(); }

    void timerCallback() override
    {
        refreshList();
        repaint();
    }

    PlayerEngine& engine;
    juce::ComboBox laneBox;
    juce::StringArray listed;
    juce::String shownId;
    juce::Rectangle<int> plot;
    bool drawing = false;
    double lastT = 0.0;
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

        addAndMakeVisible (recButton);
        recButton.setColour (juce::ToggleButton::tickColourId, cfui::px::pal::red);
        recButton.setTooltip ("Arm automation recording: while the file plays, every knob you move is written to its lane");
        recButton.onClick = [this] { recorder.armed = recButton.getToggleState(); };

        addAndMakeVisible (readButton);
        readButton.setToggleState (true, juce::dontSendNotification);
        readButton.setColour (juce::ToggleButton::tickColourId, cfui::px::pal::green);
        readButton.setTooltip ("Play the automation lanes back (Export WAV always includes them)");
        readButton.onClick = [this] { engine.readLanes.store (readButton.getToggleState()); };

        addAndMakeVisible (lanesButton);
        lanesButton.setTooltip ("Save, load or clear the automation lanes");
        lanesButton.onClick = [this] { showLanesMenu(); };

        addAndMakeVisible (lanes);
        recorder.onLaneTouched = [this] (const juce::String& id) { lanes.show (id); };

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
                                    (juce::Component*) &statusLabel, (juce::Component*) &recButton,
                                    (juce::Component*) &readButton, (juce::Component*) &lanesButton })
            c->getProperties().set ("pxScale", 1.5f);

        setWantsKeyboardFocus (true);
        setSize (editor->getWidth(), kTopBar + kWave + kLane + editor->getHeight() + kKeys);
        startTimerHz (30);
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

        // lanes saved next to the file come back with it
        const auto laneFile = laneFileFor (file);
        if (laneFile.existsAsFile() && engine.automation.load (laneFile))
            lanes.refreshList();

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
        top.removeFromLeft (14);
        recButton.setBounds (top.removeFromLeft (64));
        top.removeFromLeft (4);
        readButton.setBounds (top.removeFromLeft (72));
        top.removeFromLeft (4);
        lanesButton.setBounds (top.removeFromLeft (84));
        audioButton.setBounds (top.removeFromRight (176));
        top.removeFromRight (10);
        gainSlider.setBounds (top.removeFromRight (200));
        gainLabel.setBounds (top.removeFromRight (56));
        statusLabel.setBounds (top.reduced (6, 0));

        waveform.setBounds (area.removeFromTop (kWave).reduced (8, 2));
        lanes.setBounds (area.removeFromTop (kLane).reduced (4, 0));
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
    static constexpr int kTopBar = 40, kWave = 64, kLane = 76, kKeys = 60;

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
                                  cf::Automation laneCopy;
                                  laneCopy.copyFrom (engine.automation);

                                  juce::MouseCursor::showWaitCursor();
                                  const auto result = cf::renderFile (engine.formatManager, currentFile, target, &state,
                                                                      laneCopy.isEmpty() ? nullptr : &laneCopy);
                                  juce::MouseCursor::hideWaitCursor();

                                  statusLabel.setText (result.wasOk() ? "Exported " + target.getFileName()
                                                                      : result.getErrorMessage(),
                                                       juce::dontSendNotification);
                              });
    }

    static juce::File laneFileFor (const juce::File& audio)
    {
        return audio.withFileExtension (cf::Automation::kFileExtension);
    }

    void showLanesMenu()
    {
        juce::PopupMenu m;
        const bool any = ! engine.automation.isEmpty();
        m.addItem (1, "Save lanes" + (currentFile.existsAsFile() ? " next to " + currentFile.getFileName() : juce::String()),
                   any && currentFile.existsAsFile());
        m.addItem (2, "Save lanes as...", any);
        m.addItem (3, "Load lanes...");
        m.addSeparator();
        m.addItem (4, "Clear the shown lane", lanes.getShownId().isNotEmpty());
        m.addItem (5, "Clear all lanes", any);

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&lanesButton), [this] (int r)
        {
            if (r == 1)
                setStatus (engine.automation.save (laneFileFor (currentFile)) ? "Lanes saved" : "Couldn't save the lanes");
            else if (r == 2 || r == 3)
            {
                const bool saving = r == 2;
                chooser = std::make_unique<juce::FileChooser> (saving ? "Save automation lanes" : "Load automation lanes",
                                                               currentFile.existsAsFile() ? laneFileFor (currentFile) : juce::File(),
                                                               juce::String ("*") + cf::Automation::kFileExtension);
                const int flags = saving ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting)
                                         : juce::FileBrowserComponent::openMode;
                chooser->launchAsync (flags | juce::FileBrowserComponent::canSelectFiles, [this, saving] (const juce::FileChooser& fc)
                {
                    auto f = fc.getResult();
                    if (f == juce::File())
                        return;
                    if (saving)
                        setStatus (engine.automation.save (f.withFileExtension (cf::Automation::kFileExtension)) ? "Lanes saved" : "Couldn't save the lanes");
                    else
                    {
                        setStatus (engine.automation.load (f) ? "Lanes loaded" : "Not a lanes file");
                        lanes.refreshList();
                    }
                });
            }
            else if (r == 4)
            {
                engine.automation.clear (lanes.getShownId());
                lanes.refreshList();
            }
            else if (r == 5)
            {
                engine.automation.clearAll();
                lanes.refreshList();
            }
        });
    }

    void setStatus (const juce::String& text)
    {
        statusLabel.setText (text, juce::dontSendNotification);
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
        recorder.tick();
        playButton.setEnabled (engine.hasFile());
    }

    PlayerEngine engine;
    WaveformView waveform { engine };
    LaneView lanes { engine };
    AutomationRecorder recorder { engine };
    cfui::PixelLookAndFeel lnf;
    juce::MidiKeyboardComponent keyboard { engine.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
    juce::TextButton openButton { "Open..." }, playButton { "Play" }, exportButton { "Export WAV" },
                     audioButton { "Audio Settings" };
    juce::ToggleButton loopButton { "Loop" }, recButton { "REC" }, readButton { "READ" };
    juce::TextButton lanesButton { "Lanes..." };
    juce::Slider gainSlider;
    juce::Label gainLabel, statusLabel;
    juce::File currentFile;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AudioProcessorEditor> editor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlayerComponent)
};

//==============================================================================
/** Holds the player at its design size and scales it to whatever size the
    window is, so the whole UI shrinks or grows together and keeps its aspect. */
class ScaledContent final : public juce::Component
{
public:
    explicit ScaledContent (std::unique_ptr<PlayerComponent> p)
        : player (std::move (p)),
          designW (player->getWidth()),
          designH (player->getHeight())
    {
        addAndMakeVisible (*player);
        setSize (designW, designH);
    }

    int getDesignWidth() const noexcept       { return designW; }
    int getDesignHeight() const noexcept      { return designH; }
    PlayerComponent& getPlayer() noexcept     { return *player; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (kBackground);
    }

    void resized() override
    {
        const auto scale = juce::jmin ((float) getWidth() / (float) designW,
                                       (float) getHeight() / (float) designH);
        // centre it: only matters when maximised to a screen of another shape
        const auto x = ((float) getWidth()  - (float) designW * scale) * 0.5f;
        const auto y = ((float) getHeight() - (float) designH * scale) * 0.5f;

        player->setBounds (0, 0, designW, designH);
        player->setTransform (juce::AffineTransform::scale (scale).translated (x, y));
    }

    void focusGained (FocusChangeType) override
    {
        player->grabKeyboardFocus();
    }

private:
    std::unique_ptr<PlayerComponent> player;
    const int designW, designH;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScaledContent)
};

//==============================================================================
/** The player window. It is resizable at a fixed aspect ratio and always fits
    the screen it is on: when it opens, when it is dragged to another monitor,
    and when monitors are unplugged or their scaling changes. */
class MainWindow final : public juce::DocumentWindow,
                         private juce::Timer
{
public:
    explicit MainWindow (const juce::String& commandLine)
        : DocumentWindow ("ColorForge Player", kBackground,
                          juce::DocumentWindow::minimiseButton
                            | juce::DocumentWindow::maximiseButton
                            | juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);

        content = new ScaledContent (std::make_unique<PlayerComponent>());
        setContentOwned (content, true);

        const auto dw = content->getDesignWidth(), dh = content->getDesignHeight();
        setResizable (true, false);
        setResizeLimits (juce::roundToInt (dw * kMinScale), juce::roundToInt (dh * kMinScale),
                         juce::roundToInt (dw * kMaxScale), juce::roundToInt (dh * kMaxScale));
        getConstrainer()->setFixedAspectRatio ((double) dw / (double) dh);

        setVisible (true);
        fitToDisplay (true);
        displaySignature = currentDisplaySignature();
        startTimer (500);

        const auto arg = commandLine.unquoted().trim();

        if (arg.isNotEmpty())
            content->getPlayer().load (juce::File::getCurrentWorkingDirectory().getChildFile (arg));
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    void moved() override
    {
        DocumentWindow::moved();
        // Windows sends a stream of moves while dragging; refit once it settles.
        pendingFit = true;
        lastMoveMs = juce::Time::getMillisecondCounter();
    }

    void resized() override
    {
        DocumentWindow::resized();

        // A size the user picked (by dragging the border) becomes the size we go
        // back to when the window lands on a screen big enough for it.
        if (! fitting && ! isFullScreen() && ! isMinimised() && content != nullptr)
            preferredScale = (float) content->getWidth() / (float) content->getDesignWidth();
    }

private:
    static constexpr float kMinScale = 0.4f, kMaxScale = 2.0f;

    /** Bounds of every display and its scale, so a change to any of them shows up. */
    static juce::String currentDisplaySignature()
    {
        juce::String sig;
        for (const auto& d : juce::Desktop::getInstance().getDisplays().displays)
            sig << d.userArea.toString() << '@' << d.scale << ';';
        return sig;
    }

    juce::Rectangle<int> currentWorkArea() const
    {
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        const auto* display = displays.getDisplayForRect (getScreenBounds());

        if (display == nullptr)
            display = displays.getPrimaryDisplay();

        return display != nullptr ? display->userArea : juce::Rectangle<int> (0, 0, 1280, 720);
    }

    void fitToDisplay (bool centre)
    {
        if (content == nullptr || isMinimised())
            return;

        const auto area = currentWorkArea();

        if (isFullScreen())
        {
            // a maximised window follows the OS; just make sure it's on this screen
            if (! area.intersects (getScreenBounds()))
                setBounds (area);
            return;
        }

        // room for the native frame and title bar
        juce::BorderSize<int> frame (32, 8, 8, 8);
        if (auto* peer = getPeer())
            if (const auto f = peer->getFrameSizeIfPresent())
                frame = *f;

        const auto availW = area.getWidth()  - frame.getLeftAndRight();
        const auto availH = area.getHeight() - frame.getTopAndBottom();
        const auto dw = content->getDesignWidth(), dh = content->getDesignHeight();

        const auto scale = juce::jlimit (kMinScale, kMaxScale,
                                         juce::jmin (preferredScale,
                                                     (float) availW / (float) dw,
                                                     (float) availH / (float) dh));

        const auto w = juce::roundToInt ((float) dw * scale);
        const auto h = juce::roundToInt ((float) dh * scale);

        // keep the window where it is unless that pushes it off this screen
        auto inner = area.withTrimmedLeft (frame.getLeft()).withTrimmedRight (frame.getRight())
                         .withTrimmedTop (frame.getTop()).withTrimmedBottom (frame.getBottom());
        auto bounds = centre ? inner.withSizeKeepingCentre (w, h)
                             : getBounds().withSize (w, h).constrainedWithin (inner);

        if (bounds != getBounds())
        {
            const juce::ScopedValueSetter<bool> svs (fitting, true);
            setBounds (bounds);
        }
    }

    void timerCallback() override
    {
        const auto sig = currentDisplaySignature();

        if (sig != displaySignature)
        {
            // a monitor was plugged in or out, or its scaling changed
            displaySignature = sig;
            fitToDisplay (! currentWorkArea().intersects (getScreenBounds()));
            pendingFit = false;
            return;
        }

        // the real button state: native title-bar drags never reach JUCE as mouse events
        const bool mouseDown = juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown();

        if (pendingFit && ! mouseDown && juce::Time::getMillisecondCounter() - lastMoveMs > 300)
        {
            pendingFit = false;
            fitToDisplay (false);
        }
    }

    ScaledContent* content = nullptr;
    float preferredScale = 1.0f;
    bool fitting = false, pendingFit = false;
    juce::uint32 lastMoveMs = 0;
    juce::String displaySignature;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

//==============================================================================
class PlayerApplication final : public juce::JUCEApplication
{
public:
    PlayerApplication() = default;

    const juce::String getApplicationName() override        { return "ColorForge Player"; }
    const juce::String getApplicationVersion() override     { return "2.1.1"; }
    bool moreThanOneInstanceAllowed() override              { return true; }

    void initialise (const juce::String& commandLine) override
    {
        // Headless mode:  "ColorForge Player.exe" --render <input> <output.wav> [lanes.cfauto]
        const auto args = getCommandLineParameterArray();

        if (args.size() >= 3 && args[0] == "--render")
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();

            const auto cwd = juce::File::getCurrentWorkingDirectory();
            cf::Automation lanes;
            if (args.size() >= 4 && ! lanes.load (cwd.getChildFile (args[3])))
            {
                std::fprintf (stderr, "Can't read the lanes file %s\n", args[3].toRawUTF8());
                setApplicationReturnValue (1);
                quit();
                return;
            }
            const auto result = cf::renderFile (formats, cwd.getChildFile (args[1]), cwd.getChildFile (args[2]), nullptr,
                                                lanes.isEmpty() ? nullptr : &lanes);
            if (result.failed())
                std::fprintf (stderr, "%s\n", result.getErrorMessage().toRawUTF8());

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

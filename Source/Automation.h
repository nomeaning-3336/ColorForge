#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace cf {

/** Automation lanes for the standalone player: one breakpoint list per parameter, in
    seconds of file time and normalised (0..1) values. Continuous parameters interpolate
    linearly between points; switches and choices hold the last point.

    Editing (recording, drawing, loading) happens on the message thread under a lock. The
    audio thread only reads, with a try-lock, so it never waits: if an edit is in progress
    it simply keeps the previous values for that slice. */
class Automation
{
public:
    static constexpr const char* kFileExtension = ".cfauto";

    struct Point { double t; float v; };

    //==============================================================================
    // message thread

    /** Touch-write: replaces whatever the lane held in (from, to] with one point at `to`.
        When `to < from` the loop wrapped, so (from, end] and [0, to] are both replaced. */
    void write (const juce::String& id, double from, double to, float value)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        auto& pts = laneFor (id).points;
        auto eraseRange = [&pts] (double lo, double hi, bool includeLo)
        {
            pts.erase (std::remove_if (pts.begin(), pts.end(), [=] (const Point& p)
                       { return (includeLo ? p.t >= lo : p.t > lo) && p.t <= hi; }), pts.end());
        };
        if (to >= from)
            eraseRange (from, to, false);
        else
        {
            eraseRange (from, 1.0e12, false);
            eraseRange (0.0, to, true);
        }
        insertSorted (pts, { std::max (0.0, to), juce::jlimit (0.0f, 1.0f, value) });
    }

    /** Starts a touch at time t: keeps the lane's old value just before it, so the earlier
        part of the curve is not bent by the new points. */
    void beginTouch (const juce::String& id, double t, float currentValue, bool discrete)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        auto& lane = laneFor (id);
        lane.discrete = discrete;
        if (! lane.points.empty() && t > 0.002)
            insertSorted (lane.points, { t - 0.001, valueOf (lane, t - 0.001) });
        else if (lane.points.empty())
            insertSorted (lane.points, { std::max (0.0, t), juce::jlimit (0.0f, 1.0f, currentValue) });
    }

    void clear (const juce::String& id)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        lanes.erase (std::remove_if (lanes.begin(), lanes.end(), [&id] (const Lane& l) { return l.id == id; }), lanes.end());
    }

    void clearAll()
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        lanes.clear();
    }

    juce::StringArray laneIds() const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        juce::StringArray ids;
        for (const auto& l : lanes)
            if (! l.points.empty())
                ids.add (l.id);
        return ids;
    }

    std::vector<Point> copyLane (const juce::String& id) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (const auto& l : lanes)
            if (l.id == id)
                return l.points;
        return {};
    }

    bool isDiscrete (const juce::String& id) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (const auto& l : lanes)
            if (l.id == id)
                return l.discrete;
        return false;
    }

    bool isEmpty() const        { return laneIds().isEmpty(); }

    /** Value of a lane at time t, or -1 when the lane is empty or missing. */
    float valueAt (const juce::String& id, double t) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (const auto& l : lanes)
            if (l.id == id && ! l.points.empty())
                return valueOf (l, t);
        return -1.0f;
    }

    /** A lane being written by a knob drag is skipped by apply(), so playback and the hand
        on the knob don't fight. */
    void setTouching (const juce::String& id, bool touching)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        laneFor (id).touching = touching;
    }

    //==============================================================================
    // audio or render thread

    /** Sets every lane's parameter to its value at time t. With `wait == false` (the audio
        thread) it gives up at once if an edit holds the lock. */
    void apply (juce::AudioProcessorValueTreeState& state, double t, bool wait = false) const
    {
        auto run = [&]
        {
            for (const auto& l : lanes)
            {
                if (l.points.empty() || l.touching)
                    continue;
                if (auto* prm = state.getParameter (l.id))
                {
                    const float v = valueOf (l, t);
                    if (std::abs (prm->getValue() - v) > 1.0e-5f)
                        prm->setValueNotifyingHost (v);
                }
            }
        };

        if (wait)
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            run();
        }
        else
        {
            const juce::SpinLock::ScopedTryLockType sl (lock);
            if (sl.isLocked())
                run();
        }
    }

    //==============================================================================
    juce::ValueTree toValueTree() const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        juce::ValueTree root ("ColorForgeAutomation");
        root.setProperty ("version", 1, nullptr);
        for (const auto& l : lanes)
        {
            if (l.points.empty())
                continue;
            juce::ValueTree lane ("Lane");
            lane.setProperty ("param", l.id, nullptr);
            lane.setProperty ("discrete", l.discrete, nullptr);
            juce::String data;
            for (const auto& p : l.points)
                data << juce::String (p.t, 4) << ":" << juce::String (p.v, 5) << " ";
            lane.setProperty ("points", data.trimEnd(), nullptr);
            root.appendChild (lane, nullptr);
        }
        return root;
    }

    bool fromValueTree (const juce::ValueTree& root)
    {
        if (! root.hasType ("ColorForgeAutomation"))
            return false;

        std::vector<Lane> loaded;
        for (const auto& lane : root)
        {
            if (! lane.hasType ("Lane"))
                continue;
            Lane l;
            l.id = lane.getProperty ("param").toString();
            l.discrete = (bool) lane.getProperty ("discrete", false);
            for (const auto& tok : juce::StringArray::fromTokens (lane.getProperty ("points").toString(), " ", ""))
            {
                const auto t = tok.upToFirstOccurrenceOf (":", false, false).getDoubleValue();
                const auto v = tok.fromFirstOccurrenceOf (":", false, false).getFloatValue();
                insertSorted (l.points, { std::max (0.0, t), juce::jlimit (0.0f, 1.0f, v) });
            }
            if (l.id.isNotEmpty() && ! l.points.empty())
                loaded.push_back (std::move (l));
        }

        const juce::SpinLock::ScopedLockType sl (lock);
        lanes = std::move (loaded);
        return true;
    }

    bool save (const juce::File& file) const
    {
        if (auto xml = toValueTree().createXml())
            return xml->writeTo (file);
        return false;
    }

    bool load (const juce::File& file)
    {
        if (auto xml = juce::XmlDocument::parse (file))
            return fromValueTree (juce::ValueTree::fromXml (*xml));
        return false;
    }

    /** Same lanes, for an offline render that must not race the live player. */
    void copyFrom (const Automation& other)
    {
        fromValueTree (other.toValueTree());
    }

private:
    struct Lane
    {
        juce::String id;
        std::vector<Point> points;
        bool discrete = false, touching = false;
    };

    static void insertSorted (std::vector<Point>& pts, Point p)
    {
        auto it = std::lower_bound (pts.begin(), pts.end(), p.t, [] (const Point& a, double t) { return a.t < t; });
        if (it != pts.end() && std::abs (it->t - p.t) < 1.0e-6)
            it->v = p.v;
        else
            pts.insert (it, p);
    }

    static float valueOf (const Lane& l, double t)
    {
        const auto& pts = l.points;
        if (t <= pts.front().t) return pts.front().v;
        if (t >= pts.back().t)  return pts.back().v;
        auto hi = std::upper_bound (pts.begin(), pts.end(), t, [] (double x, const Point& p) { return x < p.t; });
        auto lo = hi - 1;
        if (l.discrete)
            return lo->v;
        const double f = (t - lo->t) / std::max (1.0e-9, hi->t - lo->t);
        return (float) (lo->v + f * (hi->v - lo->v));
    }

    Lane& laneFor (const juce::String& id)
    {
        for (auto& l : lanes)
            if (l.id == id)
                return l;
        lanes.push_back ({ id, {}, false, false });
        return lanes.back();
    }

    mutable juce::SpinLock lock;
    std::vector<Lane> lanes;
};

} // namespace cf

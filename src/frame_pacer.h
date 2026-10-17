// XRShell — Takt des Core-Threads (abhängigkeitsfrei, Host-Test tests/test_frame_pacer.cpp).
//
// Feste Fristen statt „jetzt + 1 Frame“: früher setzte der Run-Loop die nächste Frist auf
// (Beginn dieses Durchlaufs) + Periode. Jede Aufwachverzögerung des Threads (typisch 0,1 ms)
// verlängerte so jeden Frame — Headset-Befund 0.3.0/0.3.1: 71,6 statt 72 fps. Jetzt rückt die
// Frist um genau eine Periode weiter (next += period); die Verzögerung eines Frames holt der
// nächste wieder ein, die mittlere Rate ist exakt.
//
// Rückstand: liegt die Frist mehr als eine Periode hinter „jetzt“ (Pause, Core-Frame länger als
// zwei Perioden, Wechsel auf eine kürzere Periode), setzt der Takt neu auf („jetzt“ statt alter
// Phase). So läuft höchstens ein Frame sofort hinterher, nie eine Salve zum Aufholen.
// reset(): beim Fortsetzen nach einer Pause — der erste Frame läuft sofort, dann im Takt.
//
// Zeiten in Sekunden (double) relativ zu einem beliebigen festen Nullpunkt.

#pragma once

namespace pacing {

class FramePacer {
public:
    void reset() { mArmed = false; }

    // Nach einem retro_run: Frist für den nächsten Durchlauf. now = aktuelle Zeit, period = 1/fps.
    double next(double now, double period) {
        if (!(period > 0.0)) period = 1.0 / 60.0;
        if (!mArmed) {
            mArmed = true;
            mNext = now + period;
            return mNext;
        }
        mNext += period;
        if (now - mNext > period) {   // mehr als ein Frame im Rückstand → neu aufsetzen
            mNext = now;
            ++mResyncs;
        }
        return mNext;
    }

    unsigned resyncs() const { return mResyncs; }

private:
    double mNext = 0.0;
    bool mArmed = false;
    unsigned mResyncs = 0;
};

} // namespace pacing

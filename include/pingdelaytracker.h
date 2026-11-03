#pragma once

#include <QtGlobal>

// Estimates how much extra (queuing) delay a UDP stream is currently seeing,
// from the timestamps the peer puts in its ping packets.
//
// The peer stamps each ping with its own clock (ms since midnight for wfview,
// ms since power-on for Icom radios). The two machines' clocks have an unknown
// offset, tick at slightly different rates and can be stepped by NTP, so a
// single "offset" reading means nothing on its own. What we can measure is
//
//     offset = localArrivalTime - remoteSendTime
//            = (fixed clock offset) + (clock drift so far) + (network delay)
//
// The lowest offset seen recently corresponds to the least-delayed ping, so
// it is used as the baseline, and (offset - baseline) is the delay that the
// current ping suffered above that best case.
//
// The baseline follows a new minimum immediately (a faster clock on the
// peer, or a clock step that makes it look ahead). It only rises at the end
// of a window, to the lowest offset seen during that window. A sustained
// increase (a slower peer clock, a step that makes it look behind, or a
// longer network route) is absorbed within two windows, while a delay spike
// shorter than a window is still reported as excess.
//
// Local time must come from a monotonic clock (QElapsedTimer) so that this
// machine's own clock steps cannot affect the result.
class PingDelayTracker
{
public:
    static constexpr int DayMs = 24 * 60 * 60 * 1000;
    static constexpr qint64 WindowMs = 10000;

    // Feed one ping. Returns the current excess delay in ms (always >= 0).
    int update(qint64 localMonoMs, quint32 remoteStampMs)
    {
        const int remote = normDay(qint64(remoteStampMs));

        if (!valid) {
            valid = true;
            lastRemote = remote;
            remoteUnwrapped = remote;
            baseline = localMonoMs - remoteUnwrapped;
            windowMin = baseline;
            windowStart = localMonoMs;
            excess = 0;
            return excess;
        }

        // Peer timestamps wrap at midnight; unwrap them using the shortest
        // signed distance from the previous ping.
        remoteUnwrapped += signedDeltaDay(remote, lastRemote);
        lastRemote = remote;

        const qint64 offset = localMonoMs - remoteUnwrapped;

        if (offset < baseline)
            baseline = offset;

        if (offset < windowMin)
            windowMin = offset;

        if (localMonoMs - windowStart >= WindowMs) {
            if (windowMin > baseline)
                baseline = windowMin;
            windowMin = offset;
            windowStart = localMonoMs;
        }

        excess = int(qBound(qint64(0), offset - baseline, qint64(DayMs)));
        return excess;
    }

    void reset() { valid = false; excess = 0; }

    bool isValid() const { return valid; }
    int excessMs() const { return excess; }

    // Excess delay above which audio is considered too late to play.
    // Allows one packet (20 ms) or half the configured latency, whichever is
    // larger, on top of the configured latency.
    static int dropThresholdMs(int latencyMs)
    {
        return qMax(latencyMs + 20, latencyMs * 3 / 2);
    }

private:
    static int normDay(qint64 ms)
    {
        ms %= DayMs;
        if (ms < 0)
            ms += DayMs;
        return int(ms);
    }

    static int signedDeltaDay(int a, int b)
    {
        int d = normDay(a) - normDay(b);
        if (d > DayMs / 2)
            d -= DayMs;
        if (d < -DayMs / 2)
            d += DayMs;
        return d;
    }

    bool valid = false;
    int lastRemote = 0;
    qint64 remoteUnwrapped = 0;
    qint64 baseline = 0;
    qint64 windowMin = 0;
    qint64 windowStart = 0;
    int excess = 0;
};

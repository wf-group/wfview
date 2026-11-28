#pragma once

#include "wfviewtypes.h"

#include <QByteArray>
#include <QHash>
#include <QVariant>

// The radio status block that the FT-710 sends in every scope frame (the same block its main board sends to
// the front-panel display). wfview's data[i] is status byte i - 15. Layout from the FT-710 MAIN V01-12
// firmware: https://github.com/Reid-n0rc/FT-710_Firmware docs/code/main/display-link.md.
//
// Only fields whose coding is unambiguous are decoded. Each value has the type and units the matching CAT
// reply decoder produces, so the rest of wfview sees the same data whichever way it arrives.
QHash<funcs, QVariant> yaesuStatusBlockValues(const QByteArray &data);

// Frames are rewritten while they are sent, so a single frame can mix two refreshes. A value counts only once
// two consecutive frames agree on it.
class YaesuStatusConfirm
{
public:
    // Returns the confirmed values that differ from the last ones returned. confirmed, if given, receives
    // every value confirmed by this frame, changed or not.
    QHash<funcs, QVariant> update(const QHash<funcs, QVariant> &values, QHash<funcs, QVariant> *confirmed = nullptr);
    void reset();

private:
    QHash<funcs, QVariant> m_previous;
    QHash<funcs, QVariant> m_reported;
};

// The scope display window that the status block carries. Positions in wfview's data[]:
//   data[17]       scope mode, the rig file's ScopeModes Num (04 Center, 07 Cursor, 0A Fixed, ...)
//   data[32]       low nibble: span, the rig file's Spans Num. The high nibble is set in Cursor
//                  and Fixed (0x46, 0x88 seen) and is not the span.
//   data[52]       window mode: 0 = Center, 1 = Cursor, 2 = Fixed
//   data[144-147]  u32 BE, the display's low edge in Hz in Cursor and Fixed. The radio moves it
//                  itself in Cursor once the VFO leaves the window; in Fixed it is the fixed edge.
//                  In Center it is not the display edge (it holds VFO - span) and is not used.
// The display is start to start + span. From FT-710 captures (SPI offset = data index + 2900) checked
// against FA, SS05, SS06 and FD002 read over CAT.
struct yaesuScopeWindow
{
    static constexpr quint8 center = 0;
    static constexpr quint8 cursor = 1;
    static constexpr quint8 fixed = 2;
    static constexpr quint8 unknown = 0xff;

    quint8 mode = unknown;
    quint8 spanReg = 0;
    quint32 startHz = 0;

    bool operator==(const yaesuScopeWindow &o) const
    {
        return mode == o.mode && spanReg == o.spanReg && startHz == o.startHz;
    }
    bool operator!=(const yaesuScopeWindow &o) const { return !(*this == o); }
};

// The window mode (yaesuScopeWindow::center, cursor or fixed) of a ScopeModes Num in FT-710.rig, or
// yaesuScopeWindow::unknown.
quint8 yaesuScopeModeWindowMode(quint8 scopeMode);

// The window in one frame's status block. Its mode is unknown when the data is short or when data[52]
// disagrees with the scope mode in data[17].
yaesuScopeWindow yaesuScopeWindowValues(const QByteArray &data);

// The display edges in MHz for a Cursor or Fixed window spanHz wide. False in Center mode, when the
// window is unknown, or when there is no start or span; the caller then keeps the axis centred on the VFO.
bool yaesuScopeWindowEdges(const yaesuScopeWindow &window, quint32 spanHz, double *startMHz, double *endMHz);

// The window is used only once two consecutive frames agree on it, as for YaesuStatusConfirm. Until then
// the one confirmed before is kept (unknown at first).
class YaesuScopeWindowConfirm
{
public:
    // Returns true when this frame changed the confirmed window.
    bool update(const yaesuScopeWindow &window);
    const yaesuScopeWindow &window() const { return m_confirmed; }
    void reset();

private:
    yaesuScopeWindow m_previous;
    yaesuScopeWindow m_confirmed;
    bool m_havePrevious = false;
};

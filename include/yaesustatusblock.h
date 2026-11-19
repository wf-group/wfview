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

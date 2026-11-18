#pragma once

#include <QChar>

#include <deque>

// Morse code timing shared by the CW sidetone and the serial CW keyer.

// PARIS timing: one dit lasts 1.2 seconds at 1 WPM.
constexpr double cwDitSecondsAtOneWpm = 1.2;

struct CwElement
{
    bool keyDown;
    double dits;
};

// Appends the elements of one character, including the gap that follows it.
// A space is a word gap. Characters match the Icom table; unknown characters
// are sent as '?'.
void appendCwElements(QChar ch, double dashRatio, std::deque<CwElement>& elements);

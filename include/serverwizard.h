#ifndef SERVERWIZARD_H
#define SERVERWIZARD_H

#ifdef BUILD_WFSERVER

#include <QString>

namespace serverwizard {
    // Runs the interactive setup wizard. Returns 0 on success (settings saved),
    // non-zero on cancellation or error. settingsFile may be empty.
    int run(const QString& settingsFile);
}

#endif // BUILD_WFSERVER
#endif // SERVERWIZARD_H

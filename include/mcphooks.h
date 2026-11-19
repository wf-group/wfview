#pragma once

// One-line hooks that feed the optional MCP diagnostics server. Each expands
// to nothing unless wfview is built with qmake CONFIG+=mcp (WFVIEW_MCP).
// When MCP is built but not running, or traffic capture is off, a hook costs
// one atomic load.

#ifdef WFVIEW_MCP

#include "mcpaudiodiag.h"
#include "mcpprobe.h"

// Audio timing diagnostics (mcpaudiodiag.h): the statement runs only while an
// MCP client has them turned on, for example
//   MCP_AUDIO_DIAG(McpAudioDiag::event(McpAudioDiag::RadioTxPacket, bytes));
// Without CONFIG+=mcp the statement is not compiled at all.
#define MCP_AUDIO_DIAG(statement)                                                                                     \
    do                                                                                                                \
    {                                                                                                                 \
        if (McpAudioDiag::active())                                                                                   \
        {                                                                                                             \
            statement;                                                                                                \
        }                                                                                                             \
    } while (0)

#define MCP_LOG_MESSAGE(type, category, msg) McpProbe::logMessage((type), (category), (msg))
#define MCP_ATTACH_RIG(rig) McpProbe::attachRig(rig)
#define MCP_ATTACH_AUDIO(handler, setup, nativeFormat, radioFormat)                                                   \
    McpProbe::attachAudioHandler((handler), (setup), (nativeFormat), (radioFormat))
#define MCP_DETACH_AUDIO(handler) McpProbe::detachAudioHandler(handler)
// Command bytes (CI-V or CAT) to and from the radio, whatever the transport.
#define MCP_RADIO_TX(source, data) McpProbe::radioTraffic((source), true, (data))
#define MCP_RADIO_RX(source, data) McpProbe::radioTraffic((source), false, (data))
// Network control-channel packets. Sources other than control channels are
// ignored, so these can sit on paths shared with audio.
#define MCP_NETWORK_TX(source, data) McpProbe::networkTraffic((source), true, (data))
#define MCP_NETWORK_RX(source, data) McpProbe::networkTraffic((source), false, (data))
// CW key line edges from the serial keyer, on the keyer's message clock (ns):
// when the edge was due, and when the driver call to set the line started and
// returned. Recorded only while MCP runs.
#define MCP_CW_KEY_EDGE(down, dueNs, startNs, endNs)                                                                  \
    do                                                                                                                \
    {                                                                                                                 \
        if (McpProbe::enabled())                                                                                      \
            McpProbe::cwKeyEdge((down), (dueNs), (startNs), (endNs));                                                 \
    } while (0)
// CW keying events (keyer start and stop, LAN key requests and states). The
// detail expression is only evaluated while MCP runs.
#define MCP_CW_EVENT(source, what, detail)                                                                            \
    do                                                                                                                \
    {                                                                                                                 \
        if (McpProbe::enabled())                                                                                      \
            McpProbe::cwEvent((source), (what), (detail));                                                            \
    } while (0)
// Internal state a component publishes under a name, read with get_internal_state.
// The state expression is only evaluated while MCP runs.
#define MCP_STATE(name, state)                                                                                        \
    do                                                                                                                \
    {                                                                                                                 \
        if (McpProbe::enabled())                                                                                      \
            McpProbe::publishState((name), (state));                                                                  \
    } while (0)

#else

#define MCP_LOG_MESSAGE(type, category, msg) ((void)0)
#define MCP_ATTACH_RIG(rig) ((void)0)
#define MCP_ATTACH_AUDIO(handler, setup, nativeFormat, radioFormat) ((void)0)
#define MCP_DETACH_AUDIO(handler) ((void)0)
#define MCP_RADIO_TX(source, data) ((void)0)
#define MCP_RADIO_RX(source, data) ((void)0)
#define MCP_NETWORK_TX(source, data) ((void)0)
#define MCP_NETWORK_RX(source, data) ((void)0)
#define MCP_AUDIO_DIAG(statement) ((void)0)
#define MCP_CW_KEY_EDGE(down, dueNs, startNs, endNs) ((void)0)
#define MCP_CW_EVENT(source, what, detail) ((void)0)
#define MCP_STATE(name, state) ((void)0)

#endif

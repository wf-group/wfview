# MCP diagnostics server

wfview and wfserver can run an optional [Model Context Protocol](https://modelcontextprotocol.io)
(MCP) server. It lets an AI assistant, such as Claude Code, inspect a running wfview or wfserver to
help debug problems in real time: settings, the connected radio, audio devices and levels, the command
queue and state cache, meters and the log.

It is off by default, both when building and when running.

## Building

MCP is not compiled in unless qmake is run with `CONFIG+=mcp`:

```sh
mkdir build-mcp
cd build-mcp
qmake "CONFIG+=mcp" ../wfview.pro      # or ../wfserver.pro
make -j
```

It needs nothing beyond the Qt modules wfview already uses. Without `CONFIG+=mcp` the build is the
same as before.

## Running

| Option | Meaning |
|---|---|
| `--use-mcp` | Start the MCP server. |
| `--mcp-port PORT` | TCP port (default 8765). |
| `--mcp-bind ADDRESS` | Address to listen on (default `127.0.0.1`, this computer only). |
| `--mcp-token TOKEN` | Require `Authorization: Bearer TOKEN` on every request. The `WFVIEW_MCP_TOKEN` environment variable works too and keeps the token out of the process list. |
| `--mcp-allow-write` | Allow the write tools, which change settings and command the radio. Without it the server is read-only and the write tools are not even listed. |

Listening on anything other than a loopback address requires a token; wfview refuses to start the
MCP server otherwise. A build without MCP prints a notice and ignores `--use-mcp`.

In wfview (not wfserver) the same options are under Settings > Experimental > MCP Diagnostics Server,
where the server can be started, stopped and reconfigured while wfview runs. The options there start
from the command line and are never saved to the settings file. Every change restarts the server;
clients carry on with their next request. Turning MCP off unkeys the transmitter if an MCP client keyed
it. The status field lists the clients seen since the server last started.

The server uses the MCP Streamable HTTP transport at `http://ADDRESS:PORT/mcp`, answering each request
with JSON.

### Connecting Claude Code

```sh
claude mcp add --transport http wfview http://127.0.0.1:8765/mcp
# with a token:
claude mcp add --transport http wfview http://127.0.0.1:8765/mcp --header "Authorization: Bearer TOKEN"
```

To debug a user's machine remotely, the user starts wfview with
`--use-mcp --mcp-bind 0.0.0.0 --mcp-token SOMETHING-LONG` and shares the address and token. Use a VPN
or SSH tunnel rather than exposing the port to the internet; the server does not use TLS.

## Tools

### Read tools

None of these change the radio or the application. `set_traffic_capture` only switches MCP's own
recording on or off.

| Tool | Returns |
|---|---|
| `get_overview` | Start here: build, MCP mode, settings file, radio, main-thread health, audio summary, cache and queue sizes, application state. |
| `get_main_thread_health` | Whether the GUI (or wfserver main) thread is responsive, its round-trip time and the longest stall. |
| `get_windows` | Top-level windows with title, type, visibility, size, position, size limits and screen, plus each screen's usable area and scale. |
| `get_settings` | Saved settings as key/value pairs, filterable by group or text. |
| `get_settings_file` | Path, size, modification time and raw text of the settings file. |
| `get_connection` | Connection type and status, serial port and baud rate, network addresses and ports, wfshare, rigctld, TCI, cluster and server settings. |
| `get_rig_capabilities` | The radio's capabilities and rig file; `sections` adds modes, commands, bands, meters and more. |
| `get_rig_files` | Rig definition folders and the rig files found. |
| `get_audio_config` | Audio system, devices, sample rates, codecs, latency, gains, active audio paths and processing. |
| `get_audio_levels` | Latest peak/RMS, latency, underruns and update age for every open audio device, plus network audio status. |
| `get_cache` | The radio state cache, with request and reply times. |
| `get_queue` | Commands waiting to be sent to the radio. |
| `get_meters` | Latest meter readings from the cache. |
| `get_log` | Recent log lines, filterable by level, category and text. |
| `get_spectrum` | The latest scope sweep: frequency range, levels reduced to N points, noise floor and strongest signals. |
| `set_traffic_capture` | Start (for up to 15 minutes) or stop recording radio and network traffic. |
| `get_traffic` | Captured traffic as hex, and as text for Kenwood/Yaesu CAT, filterable by kind, direction and content. |
| `get_setting_options` | Settings that can be changed by name, with their current values (including unsaved changes). |
| `get_radio_server` | The built-in radio server: enabled, running or why not, protocol, address, ports and audio devices, who is logged in from where, logins since it started, and each user's access level and status. wfview only. |

### Write tools

Only with `--mcp-allow-write`. Each call is logged at info level in the `mcp` log category.

| Tool | Does |
|---|---|
| `set_setting` | Changes one setting by name through the same path as the settings dialog, so it applies at once. Not saved to the settings file unless `save` is true. |
| `set_frequency` | Tunes a receiver to a frequency in Hz. |
| `set_mode` | Sets the mode by name, keeping the current data mode and filter unless given. |
| `set_ptt` | Keys or unkeys the transmitter. Keying always has a time limit (default 10 s, at most 120 s), after which MCP unkeys the radio. In wfview it goes through the normal PTT path and honours `Controls.EnablePTT`. |
| `send_command` | Reads or sets any radio function by its cache name (for example `AF Gain`). The value type comes from the cached value. |
| `send_raw` | Sends bytes as given (hex or text) and returns the radio traffic that follows. |
| `invalidate_cache` | Removes one function's cache entries and asks the radio again, or empties the cache. |
| `set_rigctld` | Turns wfview's rigctld server on or off and sets its port. |
| `rigctld_command` | Sends a command line to wfview's rigctld over a local TCP connection and returns the reply. |
| `set_radio_server` | Turns the radio server on or off and changes an existing user's access level (`admin`, `normal`, `noTx`, `disabled`). A disabled or removed user is disconnected at once. wfview only. |

Frequency, mode, commands and raw bytes all go through the command queue, like rigctld and TCI
requests. Settings are limited to an allow-list of groups (`AudioProc`, `Cluster`, `Color`,
`Controls`, `Experimental`, `Interface`, `LAN`, `Radio`, `Server`, `UDP`); passwords can never be
changed, since a client only ever sees them masked. wfserver supports the radio tools and
`set_ptt`, but not the settings or rigctld tools.

Radios are identified by `radioId`, given in decimal and hex. For Icom it is the CI-V address; for
Kenwood and Yaesu it is the model ID.

### Traffic capture

Capture is off until a client starts it, and stops by itself when its time runs out. It records:

- **Radio traffic:** CI-V or CAT bytes to and from the radio, whatever the transport (serial, LAN,
  wfshare), including commands from VSP and TCP clients. Spectrum data from the radio is skipped
  unless `includeScope` is set, because it would fill the buffer within seconds.
- **Network control traffic:** Icom and Yaesu LAN control-channel packets. Icom pings and idle
  packets are left out. Audio, scope and CI-V sockets are not recorded at this level; CI-V is already
  in the radio traffic.

Up to 5000 radio and 2000 network entries are kept, each with at most 1024 bytes.

Passwords are masked everywhere, including the raw settings file: the first and last characters are
kept with `XXX` between them (`secret` becomes `sXXXt`), and values of one or two characters become
`XXX`.

## Design notes

wfview is known for low-latency audio and a responsive GUI, so MCP is built to stay out of the way:

- **Own thread.** Listening, HTTP, JSON-RPC and tool work all run on a low-priority `mcpServer()`
  thread. MCP keeps working when the GUI is busy or frozen, and can report that it is.
- **Main-thread data with a timeout.** Settings structures, connection preferences and controller state
  belong to the main thread, so tools ask it for a copy and give up after two seconds instead of
  reading it unsafely or waiting forever.
- **Lock-free audio taps.** Every audio device's level signal updates a slot of atomics; nothing on an
  audio thread takes a lock or allocates memory for MCP.
- **Non-blocking radio taps.** Network status updates use `tryLock` and are skipped rather than
  waiting if MCP is reading at that moment.
- **Short queue locks.** The cache and queue tools copy entries under the queue's lock and format them
  afterwards.
- **Writes use existing paths.** Radio commands go through the command queue and settings through
  `SettingsController::setOption()`, so wfview reacts exactly as it would to the GUI, rigctld or TCI.
- **Capture only on request.** Traffic hooks cost one atomic check unless a capture is running, and
  a capture always ends by itself.
- **Nothing when off.** Each hook is a single atomic check when MCP is built but not running, and is
  not compiled at all without `CONFIG+=mcp`. While running, the only steady cost is a 4 Hz heartbeat
  timer on the main thread.

### Code

| File | Role |
|---|---|
| `mcpserver.*` | HTTP and JSON-RPC on the MCP thread; startup checks. |
| `mcptools.*` | Tool definitions, JSON conversion and password masking. |
| `mcpprobe.*` | Thread-safe buffers fed by hooks: log lines, audio levels, network status, rig capabilities, traffic. |
| `mcphooks.h` | The hook macros; they expand to nothing without `CONFIG+=mcp`. |
| `mcphost.*` | Main-thread base class with the heartbeat. |
| `mcphostwfview.*`, `mcphostserver.*` | Read `MainController` or `servermain` state; each is a friend of its class. |

Hooks in existing code are one-line `MCP_...` macros from `mcphooks.h`: in `audioHandlerBase::init()`,
the `rigCommander` constructors, both log message handlers, the commanders' send and receive paths, and
the Icom and Yaesu UDP base classes. `main.cpp` holds the option parsing and start-up.

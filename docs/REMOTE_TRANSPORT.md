# Remote Transport Plan

The current native radio protocols assume direct reachability between the
client/server and the radio side. That works on a LAN, but it requires inbound
port mappings for remote operation. The planned wfshare support should avoid
rewriting each commander by introducing a transport layer underneath the
existing protocol logic. wfshare is implemented over an Asterisk/IAX relay, but
that protocol detail should not be exposed in normal user-facing UI.

## Shape

Keep the Icom, Yaesu, and Kenwood commanders responsible for radio semantics:
command selection, rig file interpretation, queueing, parsing, and capability
handling. Put direct LAN/serial tunnelling and future wfshare routing behind a
common transport API.

The initial transport API is `RadioTransport` in `include/radiotransport.h`.
It separates payload type with `RadioTransportChannel` values such as
`Control`, `Civ`, `Cat`, `AudioRx`, `AudioTx`, and `Scope`. This matters
because Icom and Yaesu use several UDP-like channels, while Kenwood LAN is
currently closer to a TCP stream.

## wfshare Encapsulation

wfshare should encapsulate radio payloads as opaque channel frames. For
example, Icom CI-V bytes should be carried on the `Civ` channel and
Kenwood/Yaesu CAT bytes should be carried on `Cat`. The relay layer should not
parse, rewrite, or coalesce radio commands; it should only preserve ordering
for stream-like channels and packet boundaries for datagram-like channels.

The initial wfview payload frame is implemented in
`radioTransportFrame::encode()`/`decode()`. It carries a magic/version marker,
transport channel, sequence number, and opaque payload bytes. This is the frame
that should be carried inside the eventual relay call.

`IaxRadioTransport` provides the current low-level Asterisk/IAX implementation
used by wfshare.

The client side places the configured Station ID in the IAX called-number
field. The server side can register a wfshare station from wfserver settings
under the `Server` group:

```ini
[Server]
WfShareEnabled=true
WfShareHost=pbx.wfshare.org
WfSharePort=4569
WfShareUsername=50001
WfSharePassword=secret
```

At this stage registration and incoming call acceptance are implemented as the
transport handshake. Routing authenticated wfshare frames into the server's
radio/audio paths is the next integration step.

This keeps the existing commanders usable with both direct and relayed
connections. The commander asks the transport to send CAT/CI-V/audio/scope
data, and the selected transport decides whether that means local TCP/UDP or a
wfshare relay frame through Asterisk.

## Implementation Stages

1. Add direct transport adapters that wrap the current `QTcpSocket` and
   `QUdpSocket` behavior without changing packet formats.
2. Move one low-risk path first, most likely Kenwood LAN control, because it is
   already stream oriented.
3. Add Icom/Yaesu adapters once the multi-channel datagram model has been
   proven.
4. Add a wfshare transport that presents the same channels to commanders while
   the Asterisk side handles NAT traversal and relay.
5. Move audio only after control traffic is stable. Audio format negotiation
   and jitter handling must remain explicit, not hidden in commander code.

## Constraints

The transport layer must not change rig capability detection or command
selection. It should report connection state and errors, but radio-specific
retries, command uniqueness, and parse rules stay in the existing commander and
queue code. Direct LAN must remain the default path so local operation is not
regressed while wfshare support is developed.

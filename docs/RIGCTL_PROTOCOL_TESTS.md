# Rigctl Protocol Test Results

Date: 2026-05-25

## Test Setup

| Component | Path / endpoint | Version / role |
| --- | --- | --- |
| wfview rigctld | `127.0.0.1:4533` | Current `v3-initial` build, connected to IC-7610 |
| Hamlib 4.x client | `C:\Program Files\hamlib-w64-4.7.1\bin\rigctl.exe` | Hamlib 4.7.1 |
| Hamlib 3.x client | `D:\Software\Ham-radio\grig-0.8.1-win32\bin\rigctl.exe` | Hamlib 3.1~git from grig package |

The tests avoided destructive operations: no PTT-on, reset, raw rig command, DTMF, voice memory, or CW transmit commands were sent. Setter tests used benign values already expected by the current session.

## Summary

wfview is compatible with the current Hamlib 4.7.1 `rigctl` client for the tested command set, including VFO mode, mode/filter-width changes, and antenna get/set.

The grig-bundled Hamlib 3.1 client is only partially compatible. Without `--vfo`, common getters and mode/filter-width setters work, but antenna set using the current `Y <ant> <option>` form fails because the old client treats the option as a separate command. With `--vfo`, Hamlib 3.1 fails many commands locally or reports protocol errors before wfview can handle them. This matches the observed grig behavior and should be treated as an old-client compatibility limitation rather than a current Hamlib regression.

As a reference baseline, Hamlib 3.1 `rigctl` was also tested against Hamlib 4.7.1 `rigctld` running the built-in Dummy backend. The same old-client antenna failures occur there, so those failures are not specific to wfview's rigctld implementation.

## Command Matrix

Legend: `OK` means the command completed without rigctl protocol errors. `FAIL` means the client returned a protocol error, rejected the command locally, or split arguments incorrectly.

| Command | Args tested | Hamlib 4.7.1 `--vfo` | Hamlib 3.1 | Hamlib 3.1 `--vfo` | Notes |
| --- | --- | --- | --- | --- | --- |
| get_freq | `f` | OK | OK | FAIL | 3.1 `--vfo` reports protocol error |
| get_mode | `m` | OK | OK | FAIL | 3.1 `--vfo` reports protocol error |
| get_ptt | `t` | OK | OK | FAIL | PTT read only |
| get_vfo | `v` | OK | OK | FAIL | 3.1 `--vfo` output was inconsistent |
| set_vfo help | `V ?` | OK | OK | OK | Lists `Main`, `Sub`, `MEM` |
| dump_caps | `1` | OK | OK | OK | Capability dump parsed |
| get_split_vfo | `s` | OK | OK | FAIL | 3.1 `--vfo` reports protocol error |
| level help | `l ?` | OK | OK | FAIL | 3.1 non-VFO uses older bitmap names |
| function help | `u ?` | OK | OK | FAIL | 3.1 non-VFO uses older bitmap names |
| parameter help | `p ?` | OK | OK | OK | Advertises `TIME` |
| get S-meter | `l STRENGTH` | OK | OK | FAIL | Returned current cached meter value |
| get key speed | `l KEYSPD` | OK | OK | FAIL | Returned `6` in this run |
| get AF gain | `l AF` | OK | OK | FAIL | Returned normalized float |
| get NB | `u NB` | OK | OK | FAIL | Returned current cached state |
| get time parm | `p TIME` | OK | OK | OK | Seconds since midnight |
| set frequency same | `F 18100000` | OK | OK | FAIL | Non-destructive same-frequency test |
| set mode normal | `M USB 2500` | OK | OK | FAIL | 3.1 `--vfo` rejects set-mode path |
| set mode narrow | `M USB 1800` | OK | OK | FAIL | Filter-width command path works with current Hamlib |
| set mode wide | `M USB 3000` | OK | OK | FAIL | Filter-width command path works with current Hamlib |
| get antenna | `y 1` | OK | OK | FAIL | Current Hamlib returns named antenna fields |
| set antenna 1 | `Y 1 0` | OK | FAIL | FAIL | 3.1 treats `0` option as a separate command |
| set antenna 2 | `Y 2 0` | OK | FAIL | FAIL | Current Hamlib sends `ant=0x02, option=0` |

## Long Commands

`dump_state` was also checked directly over the TCP rigctld socket because both rigctl CLIs are awkward to drive with long commands from PowerShell. wfview returned a valid dump containing:

- antenna range mask `0x3`
- `has_get_ant=0x1`
- `has_set_ant=0x1`
- `targetable_vfo=0x3`
- `done`

## Reference Baseline: Hamlib 3.1 Client to Hamlib 4.7.1 rigctld

This baseline used:

- Server: `C:\Program Files\hamlib-w64-4.7.1\bin\rigctld.exe -m 1 -T 127.0.0.1 -t 4534`
- Client: `D:\Software\Ham-radio\grig-0.8.1-win32\bin\rigctl.exe -m 2 -r 127.0.0.1:4534`
- Backend: Hamlib Dummy, not a real radio

The explicit-VFO column used `--vfo` with a `Main` VFO argument where the old client required one.

| Command | Standard args | Hamlib 3.1 -> 4.7.1 | Explicit-VFO args | Hamlib 3.1 `--vfo` -> 4.7.1 | Notes |
| --- | --- | --- | --- | --- | --- |
| get_freq | `f` | OK | `f Main` | OK | Returned `145000000` |
| get_mode | `m` | OK | `m Main` | OK | Returned `FM 15000` |
| get_ptt | `t` | OK | `t Main` | OK | Returned `0` |
| get_vfo | `v` | OK | `v` | OK* | `--vfo` exited OK but printed an extra invalid-arg diagnostic |
| set_vfo help | `V ?` | OK | `V ?` | OK | Listed `MEM Main Sub VFOA VFOB VFOC` |
| dump_caps | `1` | OK | `1` | OK | Dump parsed; client reports NET rigctl backend metadata |
| get_split_vfo | `s` | OK | `s Main` | OK | Returned split disabled |
| level help | `l ?` | OK | `l ?` | OK* | `--vfo` exited OK but printed an invalid-arg diagnostic |
| function help | `u ?` | OK | `u ?` | OK* | `--vfo` exited OK but printed an invalid-arg diagnostic |
| parameter help | `p ?` | OK | `p ?` | OK | Listed `ANN APO BACKLIGHT BEEP TIME BAT KEYLIGHT` |
| get S-meter | `l STRENGTH` | OK | `l Main STRENGTH` | OK | Returned `-53` |
| get key speed | `l KEYSPD` | OK | `l Main KEYSPD` | OK | Returned `0` |
| get AF gain | `l AF` | OK | `l Main AF` | OK | Returned `0.000000` |
| get NB | `u NB` | OK | `u Main NB` | OK | Returned `0` |
| get time parm | `p TIME` | OK | `p Main TIME` | FAIL | Old client sends an invalid parameter shape in `--vfo` mode |
| set frequency same | `F 145000000` | OK | `F Main 145000000` | OK | Benign same-frequency write |
| set mode normal | `M USB 2500` | OK | `M Main USB 2500` | OK | Mode/filter write accepted |
| set mode narrow | `M USB 1800` | OK | `M Main USB 1800` | OK | Mode/filter write accepted |
| set mode wide | `M USB 3000` | OK | `M Main USB 3000` | OK | Mode/filter write accepted |
| get antenna | `y 1` | FAIL | `y Main 1` | FAIL | Timed out against Hamlib 4.7.1 Dummy rigctld |
| set antenna 1 | `Y 1 0` | FAIL | `Y Main 1 0` | FAIL | Timed out; old client also treats `0` as a separate command |
| set antenna 2 | `Y 2 0` | FAIL | `Y Main 2 0` | FAIL | Timed out; old client also treats `0` as a separate command |

## Follow-Up Items

- Prefer current Hamlib behavior when compatibility conflicts with Hamlib 3.1 for now.
- Revisit this after testing real clients and recording which Hamlib versions they embed. If common clients still ship older Hamlib, broader compatibility may be worth prioritizing over strict current-Hamlib behavior.
- Keep destructive rigctl operations excluded from automated tests unless a dummy backend or explicit test mode is used.
- Add a scripted regression test once wfview has a no-radio/dummy rigctld mode.

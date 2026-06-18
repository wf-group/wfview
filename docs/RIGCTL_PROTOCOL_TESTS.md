# Rigctl Protocol Test Results

Date: 2026-06-18

## Test Setup

| Component | Path / endpoint | Version / role |
| --- | --- | --- |
| wfview rigctld | `127.0.0.1:4533` | Manual real-radio matrix from 2026-05-25, connected to IC-7610 |
| wfview rigctld harness | `tests/tst_rigctld.cpp` | Current `v3-initial` build, in-process rigctld with fake rigCaps and no radio |
| Hamlib 4.x client | `C:\Program Files\hamlib-w64-4.7.1\bin\rigctl.exe` | Hamlib 4.7.1 |
| Hamlib 3.x client | `D:\Software\Ham-radio\grig-0.8.1-win32\bin\rigctl.exe` | Hamlib 3.1~git from grig package |
| Hamlib baseline server | `127.0.0.1:4534` | Hamlib 4.7.1 `rigctld -m 1` Dummy backend |

The tests avoided destructive operations: no PTT-on, reset, raw rig command, DTMF, voice memory, or CW transmit commands were sent. Setter tests used benign values already expected by the current session.

CW transmit command parsing is covered by the wfview rigctld harness. Run live CW transmit tests only with a dummy load, RF-safe test setup, or a non-transmitting test backend.

## Summary

wfview is compatible with the current Hamlib 4.7.1 `rigctl` client for the tested standard command set, including VFO selection aliases, mode/filter-width changes, VFO operations, level controls, CW pitch, and antenna get/set.

The grig-bundled Hamlib 3.1 client now matches or exceeds the Hamlib 3.1-to-Hamlib 4.7.1 Dummy baseline for the tested commands. The remaining Hamlib 3.1 failures are old-client command-shape issues or unsupported Dummy-backend behavior, not wfview-specific protocol regressions.

The automated wfview rigctld harness now covers the no-radio Morse parser path for `b`, `\send_morse`, spacing preservation, and `\stop_morse`. The full Qt test binary passed on 2026-06-18.

`OK*` means the client exited successfully but printed a harmless extra diagnostic such as `Invalid arg for command`; this behavior also appears against Hamlib 4.7.1 `rigctld`.

## Command Matrix

Legend: `OK` means the command completed without rigctl protocol errors. `FAIL` means the client returned a protocol error, rejected the command locally, printed an error diagnostic, or split arguments incorrectly. `NT` means not tested.

| Command | wfview 4.7 | wfview 3.1 | wfview 3.1 `--vfo` | 3.1 -> 4.7 Dummy | 3.1 `--vfo` -> 4.7 Dummy | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| get_freq | OK | OK | OK | OK | OK |  |
| get_mode | OK | OK | OK | OK | OK |  |
| get_ptt | OK | OK | OK | OK | OK | PTT read only |
| get_vfo | OK | OK | OK* | OK | OK* | Old client prints an extra diagnostic in `--vfo` mode |
| set_vfo help | OK | OK | OK | OK | OK | wfview advertises `MEM Main Sub VFOA VFOB` |
| set_vfo Main | OK | OK | OK | OK | OK |  |
| set_vfo VFOA | OK | OK | OK | OK | OK | `VFOA` aliases `Main` on dual-receiver rigs |
| dump_caps | OK | OK | OK | OK | OK | Capability dump parsed |
| get_split_vfo | OK | OK | OK | OK | OK |  |
| level help | OK | OK | OK* | OK | OK* | Old client prints an extra diagnostic in `--vfo` mode |
| function help | OK | OK | OK* | OK | OK* | Old client prints an extra diagnostic in `--vfo` mode |
| parameter help | OK | OK | OK | OK | OK | wfview advertises `TIME` |
| get S-meter | OK | OK | OK | OK | OK | Returned current cached meter value |
| get key speed | OK | OK | OK | OK | OK |  |
| get AF gain | OK | OK | OK | OK | OK | Returned normalized float |
| get NB | OK | OK | OK | OK | OK | Returned current cached state |
| get time parm | OK | OK | FAIL | OK | FAIL | Old `--vfo` client sends invalid parameter shape |
| set frequency same | OK | OK | OK | OK | OK | Non-destructive same-frequency write |
| set mode normal | OK | OK | OK | OK | OK |  |
| set mode narrow | OK | OK | OK | OK | OK | Filter-width command path works |
| set mode wide | OK | OK | OK | OK | OK | Filter-width command path works |
| vfo_op help | OK | OK | OK* | OK | OK* | Old client prints an extra diagnostic in `--vfo` mode |
| vfo_op copy | OK | FAIL | OK* | FAIL | OK* | Old standard client reports unavailable; explicit-VFO form matches baseline |
| vfo_op swap | OK | FAIL | OK* | FAIL | OK* | Old standard client reports unavailable; explicit-VFO form matches baseline |
| get preamp | OK | OK | OK | OK | OK |  |
| set preamp off | OK | OK | OK | OK | OK |  |
| set preamp 10 | OK | OK | OK | OK | OK |  |
| get attenuator | OK | OK | OK | OK | OK |  |
| set attenuator off | OK | OK | OK | OK | OK |  |
| set attenuator 10 | OK | OK | OK | OK | OK |  |
| get CW pitch | OK | OK | OK | OK | OK |  |
| set CW pitch 600 | OK | OK | OK | OK | OK |  |
| get antenna | OK | OK | FAIL | FAIL | FAIL | wfview standard old-client path works; old explicit-VFO path and Dummy baseline fail |
| set antenna 1 | OK | FAIL | FAIL | FAIL | FAIL | Hamlib 3.1 treats the option argument as a separate command |
| set antenna 2 | OK | FAIL | FAIL | FAIL | FAIL | Hamlib 3.1 treats the option argument as a separate command |
| send_morse short `b CQ TEST` | NT | NT | NT | FAIL | FAIL | Covered by wfview harness; Hamlib 3.1 fails against 4.7 Dummy |
| send_morse long `\send_morse CQ TEST` | NT | NT | NT | FAIL | FAIL | Covered by wfview harness; Hamlib 3.1 fails against 4.7 Dummy |
| stop_morse `\stop_morse` | NT | NT | NT | OK* | OK* | Covered by wfview harness; old client prints an extra diagnostic after success |

## Automated wfview Rigctld Harness

Run on 2026-06-18 with Qt 6.10.2/MSVC:

```powershell
cmd /v:on /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" x64 && set "PATH=C:\Qt\6.10.2\msvc2022_64\bin;!PATH!" && cd /d "C:\Users\phil.taylor\source\repos\wfview\tests-build" && "C:\Qt\Tools\QtCreator\bin\jom\jom.exe" -j %NUMBER_OF_PROCESSORS% && release\wfview-tests.exe -txt'
```

Result: full Qt test binary passed. `RigCtlDTest` reported `7 passed, 0 failed`.

| Test | Result | Notes |
| --- | --- | --- |
| `b CQ TEST` | OK | Queues `funcSendCW` with `CQ TEST` |
| `\send_morse CQ TEST` | OK | Queues `funcSendCW` with `CQ TEST` |
| `b CQ  TEST` | OK | Preserves the double space in the queued Morse text |
| `\send_morse CQ  TEST` | OK | Preserves the double space in the queued Morse text |
| `\stop_morse` | OK | Queues empty `funcSendCW` and returns `RPRT 0` |

## Hamlib Dummy Morse Checks

These tests cover both rigctl clients currently used for compatibility checks. They are intentionally separate from the real-radio matrix because they can cause a connected radio to transmit CW when run against wfview with a real radio connected.

Client-only checks against the Hamlib 4.7.1 Dummy backend were run on 2026-06-18. The Hamlib 4.7.1 checks pass if the Dummy backend is first placed in CW mode using `M CW 500`; the Hamlib 3.1 client reports protocol errors for both `send_morse` forms against the same Dummy backend.

| Client | Backend | Command | Result | Notes |
| --- | --- | --- | --- | --- |
| Hamlib 4.7.1 | Hamlib 4.7.1 Dummy | `b CQ TEST` | OK | stdin/interactive form |
| Hamlib 4.7.1 | Hamlib 4.7.1 Dummy | `\send_morse CQ TEST` | OK | stdin/interactive form |
| Hamlib 4.7.1 | Hamlib 4.7.1 Dummy | `\stop_morse` | OK | stdin/interactive form |
| Hamlib 3.1 | Hamlib 4.7.1 Dummy | `b CQ TEST` | FAIL | `send_morse: error = Protocol error` |
| Hamlib 3.1 | Hamlib 4.7.1 Dummy | `\send_morse CQ TEST` | FAIL | `send_morse: error = Protocol error` |
| Hamlib 3.1 | Hamlib 4.7.1 Dummy | `\stop_morse` | OK* | Exits successfully but prints an extra `Command '\0' not found!` diagnostic |

Hamlib 4.7.1 client:

```powershell
"b CQ TEST" | & "C:\Program Files\hamlib-w64-4.7.1\bin\rigctl.exe" -m 2 -r 127.0.0.1:4533
"\send_morse CQ TEST" | & "C:\Program Files\hamlib-w64-4.7.1\bin\rigctl.exe" -m 2 -r 127.0.0.1:4533
"\stop_morse" | & "C:\Program Files\hamlib-w64-4.7.1\bin\rigctl.exe" -m 2 -r 127.0.0.1:4533
```

Hamlib 3.1 client from the grig package:

```powershell
"b CQ TEST" | & "D:\Software\Ham-radio\grig-0.8.1-win32\bin\rigctl.exe" -m 2 -r 127.0.0.1:4533
"\send_morse CQ TEST" | & "D:\Software\Ham-radio\grig-0.8.1-win32\bin\rigctl.exe" -m 2 -r 127.0.0.1:4533
"\stop_morse" | & "D:\Software\Ham-radio\grig-0.8.1-win32\bin\rigctl.exe" -m 2 -r 127.0.0.1:4533
```

Interactive rigctl equivalents:

```text
b CQ TEST
\send_morse CQ TEST
\stop_morse
```

Spacing check:

```text
b CQ  TEST
\send_morse CQ  TEST
```

The two spaces between `CQ` and `TEST` should be preserved by wfview rigctld and passed as a single Morse string argument.

## Notes

- Current Hamlib 4.7 standard commands are the primary compatibility target.
- wfview now advertises VFO aliases consistently with the names it already accepts, so older clients can see both `Main`/`Sub` and `VFOA`/`VFOB`.
- Some Hamlib 3.1 behavior is client-side: the same failures appear when talking to Hamlib 4.7.1 `rigctld` with the Dummy backend.
- Keep destructive rigctl operations excluded from automated tests unless a dummy backend or explicit test mode is used.

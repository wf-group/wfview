# Rigctl Protocol Test Results

Date: 2026-05-25

## Test Setup

| Component | Path / endpoint | Version / role |
| --- | --- | --- |
| wfview rigctld | `127.0.0.1:4533` | Current `v3-initial` build, connected to IC-7610 |
| Hamlib 4.x client | `C:\Program Files\hamlib-w64-4.7.1\bin\rigctl.exe` | Hamlib 4.7.1 |
| Hamlib 3.x client | `D:\Software\Ham-radio\grig-0.8.1-win32\bin\rigctl.exe` | Hamlib 3.1~git from grig package |
| Hamlib baseline server | `127.0.0.1:4534` | Hamlib 4.7.1 `rigctld -m 1` Dummy backend |

The tests avoided destructive operations: no PTT-on, reset, raw rig command, DTMF, voice memory, or CW transmit commands were sent. Setter tests used benign values already expected by the current session.

## Summary

wfview is compatible with the current Hamlib 4.7.1 `rigctl` client for the tested standard command set, including VFO selection aliases, mode/filter-width changes, VFO operations, level controls, CW pitch, and antenna get/set.

The grig-bundled Hamlib 3.1 client now matches or exceeds the Hamlib 3.1-to-Hamlib 4.7.1 Dummy baseline for the tested commands. The remaining Hamlib 3.1 failures are old-client command-shape issues or unsupported Dummy-backend behavior, not wfview-specific protocol regressions.

`OK*` means the client exited successfully but printed a harmless extra diagnostic such as `Invalid arg for command`; this behavior also appears against Hamlib 4.7.1 `rigctld`.

## Command Matrix

Legend: `OK` means the command completed without rigctl protocol errors. `FAIL` means the client returned a protocol error, rejected the command locally, printed an error diagnostic, or split arguments incorrectly.

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

## Notes

- Current Hamlib 4.7 standard commands are the primary compatibility target.
- wfview now advertises VFO aliases consistently with the names it already accepts, so older clients can see both `Main`/`Sub` and `VFOA`/`VFOB`.
- Some Hamlib 3.1 behavior is client-side: the same failures appear when talking to Hamlib 4.7.1 `rigctld` with the Dummy backend.
- Keep destructive rigctl operations excluded from automated tests unless a dummy backend or explicit test mode is used.

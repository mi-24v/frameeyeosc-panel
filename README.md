# FrameEyeOSC Panel

Independent Linux C++ OpenVR Dashboard application, version **0.1.0**. It starts
and owns one `frameeyeosc` backend child. The backend owns settings, OSC and
persistence; this panel consumes the **IPC v1 settings and calibration contract** over NDJSON.
There is no Cargo, Rust source tree, or vendor fork build dependency.

## Build and local checks

Requirements: CMake 3.24+, a C++17 compiler, Linux x86_64 or AArch64, and Python 3
for tests and optional manifest generation. The initial configure downloads
SHA-256-pinned official archives: Valve OpenVR SDK v2.15.6 (commit
`41bc3825fd35b04047610c86fee26fb33b017b29`) and nlohmann JSON v3.11.3.
Dependency source/license files stay under `build/_deps`; installed dependency
licenses are copied to `share/frameeyeosc-panel/licenses`.

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/frameeyeosc-panel --help
```

Tests use a fake OpenVR runtime boundary, genuine SDK event enums, and actual
Unix sockets/owned subprocesses, including parent death, process races, exec
failure and SIGTERM-to-SIGKILL escalation. They never initialize OpenVR or use
an HMD. The frozen `tests/fixtures/ipc-v1.jsonl` includes the complete settings schema,
calibration state/results and applied profiles. Comparisons parse JSON; key order and whitespace are irrelevant.

CMake selects the official `linux64` library on x86_64 and `linuxarm64` on
AArch64. For a cross build, supply an actual AArch64 compiler/toolchain. An
optional `-DOPENVR_LIBRARY=/absolute/path/to/libopenvr_api.so` can select the
matching official installed runtime library. An AArch64 configure-selection
check is not an AArch64 compilation or a headset validation. Local automated
checks were performed on x86_64; **HMD rendering, keyboard interaction, manifest
registration/autostart, and Steam Frame execution remain unverified**.

## Launch and lifecycle

With a locally running SteamVR runtime, launch the installed panel:

```sh
frameeyeosc-panel --backend /absolute/path/to/frameeyeosc \
  --control-socket /run/user/1000/frameeyeosc-panel/control.sock \
  --config /absolute/path/to/settings.json
```

The socket's immediate parent must be a dedicated directory owned by the current
user with mode 0700. The backend creates it if absent and its parent exists,
validates existing directories, and provides the socket at 0600. Use the same
config/socket on subsequent launches. `--config` is optional and otherwise
uses the backend's normal config default. The child gets the same paths plus
`--panel-mode`; no explicit auto target overrides saved manual settings.

`--backend` must name the backend executable directly, or a wrapper that uses
`exec` to replace itself with it. A wrapper that forks a separate backend fails
the PID ownership check.

The child starts **before** `VR_Init`, with prebuilt argv, default/unblocked
termination signals, Linux parent-death SIGTERM and a parent-PID race recheck.
An exec status pipe distinguishes failure from successful exec. The panel
refuses a live preexisting backend on the same control socket. Older IPC-less
backends and instances using a different socket are not detected; stop those
manually before starting the panel. No unmanaged process is killed or adopted. Every initial/reconnected socket must have
`SO_PEERCRED.pid` equal to this owned child and the same UID, before any request
is sent. A racing foreign peer is rejected; it is never signaled or adopted.
Stale socket handling and the socket-path lock remain backend responsibilities.

Dashboard hiding, stream stop/disconnection, unplug notifications and remote
`VREvent_ProcessQuit` (701) keep the panel/backend resident. Explicit UI Close,
Close from either owned dashboard/thumbnail queue, local `VREvent_Quit` (700),
or SIGINT/SIGTERM stop only the owned child, wait up to two seconds, escalate to
SIGKILL if needed, reap it, destroy overlays and shut down OpenVR. Parent death
signals the backend. A backend crash leaves the panel showing its exit/signal
reason, without automatic restart or attachment to a replacement process.
Close the panel and **relaunch manually**. Startup without a confirmed socket
state times out after eight seconds and stops the owned child.

IPC is nonblocking and bounded: outgoing requests are limited to 64 KiB; incoming
response/event LF-terminated lines are limited to 1 MiB (the backend output queue
bound). Valid snapshots can exceed 64 KiB when current and saved values repeat
a long string. There is a 16 KiB read budget
and at most eight parsed messages per tick, and one pending request/output
line. Invalid JSON/UTF-8, non-finite numbers, duplicate keys, excessive nesting
and overlong lines
disconnect. A response timeout is three seconds; reconnect retries are spaced
by 250 ms. Every reconnect first requests `state.get`. Unconfirmed patches are
never automatically resent; calibration operations are also never replayed. A `request_limit` response triggers a reconnect and
`state.get`; its unapplied draft requires a manual Apply after synchronization.
An error response to `state.get` also reconnects and retries `state.get` only,
with 250 ms reconnect spacing while it fails.
Last confirmed values are visibly **STALE** on
connection loss. OS/exec/filesystem operations have no hard real-time guarantee.

## Settings and input

The dashboard displays each supported field's current, saved and source values:
cap, prefix, One Euro enable plus all three coefficients, target mode/host/port,
and ABI `auto`/`4`/`5` on the Settings tab. The Calibration tab contains
`eye_widening`, left/right margins, `min_base_width` and `min_wide_width`,
with the same current/saved/source columns and draft Apply/Save behavior.
Margins must be finite and nonnegative; minimum widths must be in `(0,1]`.
The 1440x1080 dashboard uses separate pages to keep all rows and controls visible. It separately shows tracking, stream state/observed peer,
actual OSC target, output state/reason and backend stopped reason. A stream peer
is not inferred to be the OSC target; sending does not guarantee UDP reception.

Click a field. Boolean, mode and ABI rows cycle their supported values; other
rows open the SDK keyboard. Printable ASCII entry is supported; case is preserved
in values and the original bitmap font (OSC paths are case sensitive). Values
are clipped to their table column widths (with a visible `>` marker); keyboard entry is limited to 1024 characters. Longer CLI/saved
values stay valid and appear clipped; editing replaces them with a value within
this keyboard limit. Non-ASCII glyphs currently appear as `?`. Local type/range checks show an error; the backend remains authoritative
for candidate validation, resource preparation and saving. Snapshot updates
never overwrite an active draft. Keyboard results are accepted only for this
overlay and the matching request token. Nonzero-token Close notifications use
the request token even if Opened was not received; tokenless Close notifications
require a matching overlay's Opened notification. After Close, the panel waits
up to 250ms for Done from another event queue. During this grace period, up to
64 clicks other than Close are deferred and replayed after Done or the deadline;
the 65th and later clicks are dropped. Close is always processed immediately.
A keyboard-open failure is shown.

Toggle **Save**, then **Apply changes** to send only selected fields. A Save:false
trial retains selected unsaved drafts, so Save:true can persist them even when
they already match current values. Explicitly editing a current value also
selects that field for saving; unrelated CLI/current fields are not saved.
Use **Discard drafts** to clear all selected edits before saving another field;
this leaves backend current/saved values unchanged. Edits and
Apply are disabled while awaiting a backend response. Current values do not
change optimistically. Failures retain the backend's actual state and display
its error; failed drafts can be corrected. Saved fields update only after a
confirmed successful save. Runtime patches may override CLI values; subsequent
backend launches apply CLI > saved > default precedence again.

The CPU bitmap is submitted with `SetOverlayRaw` only when its overlay is visible
and its state/UI changes. Owned SDK ImageFailed events invalidate cached images
and retry while visible at 250 ms intervals, up to three consecutive failures;
stderr and the dashboard show the failure. Relaunch manually after exhaustion.
The main bitmap is 1440x1080 RGBA (6,220,800 bytes); its acceptance by an actual
runtime remains unverified. The separate thumbnail is 256x128 RGBA.
No OpenGL/UI framework is required. Calibration is
shown as **Calibration unavailable in this build**. No calibration buttons,
profiles, widening/margin controls or numeric calibration defaults are supplied.

## Manual calibration

Open the Calibration tab and click **Start**. Each waiting stage is started
explicitly with **Begin stage**: closed eyes, natural opening, then optional
wide opening. Preparing lasts two seconds and measuring lasts five seconds;
the dashboard shows the backend's stage, state and remaining milliseconds.
No next stage starts automatically. At the wide waiting stage, **Skip wide**
finishes with the newly measured base profile. **Cancel** is available during
waiting, preparing and measuring, including while an operation response is
pending. A queued Cancel runs after that response using its actual session;
a rejected Start does not cancel a previous terminal session.

The applied left/right measurements, natural maximum, derived `d` and
`wide_start`, wide availability and unavailable reason are shown independently
from the candidate calibration. State/results retain their `caused_by` IDs.
A stage failure returns to the same waiting stage for manual retry. For an
invalid natural interval, retry natural opening; to remeasure closed eyes,
Cancel and Start again. Saving failure shows `failed`/`save_failed` and retains
the old applied profile. Acceptance of Start or Begin is not completion; the
backend replaces a profile only after its final atomic save. Calibration
persistence is automatic and independent of the settings Save toggle.

On IPC loss, settings edits and calibration actions remain disabled until a
fresh `state.get` snapshot arrives. The initiating connection's loss causes
the backend to cancel an active session; reconnect shows its actual terminal
state and never restarts measurement. Close continues to stop the owned backend.

## Install and manual manifest registration

Generate the manifest for final, version-independent install paths. Replace the
example UID and backend/config paths with the target machine's actual paths.
Setting `CMAKE_INSTALL_LIBDIR=lib` keeps the example library location explicit.
Choose the prefix at configure time; changing `cmake --install --prefix` later
would leave the configured manifest pointing at the old binary location.

```sh
prefix="$HOME/.local"
cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DCMAKE_INSTALL_LIBDIR=lib \
  -DPANEL_BACKEND="$prefix/bin/frameeyeosc" \
  -DPANEL_CONTROL_SOCKET="/run/user/1000/frameeyeosc-panel/control.sock" \
  -DPANEL_CONFIG="$HOME/.config/frameeyeosc/settings.json"
cmake --build build -j2
cmake --install build
```

The generated/installed `share/frameeyeosc-panel/frameeyeosc-panel.vrmanifest`
uses app key `frameeyeosc.panel`, `is_dashboard_overlay=true`, and both
`binary_path_linux` and Steam Frame's `binary_path_linux_arm`. Both launch only
the panel; its arguments identify the backend. Build/install do not register or
start the application. Both PANEL_BACKEND and PANEL_CONTROL_SOCKET must be
provided together; PANEL_CONFIG is optional. Manifest generation accepts only
absolute ASCII paths that require no shell quotation (the Python shlex.quote
result must equal the original path). Spaces, quotes, backslashes, shell
metacharacters requiring quotation, non-ASCII and control characters are
rejected because SteamVR argument parsing has not been verified. This applies
to the installed panel binary path and every backend/socket/config path. Direct
CLI invocation via actual argv can use other paths; installing without manifest
options supports such prefixes.

Registration is a separate **manual** action on the target machine with SteamVR
running. Use the official SDK application APIs, checking each returned error:

```cpp
#include <openvr.h>
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 2) return 2; // absolute installed manifest path
    vr::EVRInitError init = vr::VRInitError_None;
    vr::VR_Init(&init, vr::VRApplication_Utility);
    if (init != vr::VRInitError_None) return 1;
    auto added = vr::VRApplications()->AddApplicationManifest(argv[1], false);
    auto enabled = added == vr::VRApplicationError_None
        ? vr::VRApplications()->SetApplicationAutoLaunch("frameeyeosc.panel", true)
        : added;
    std::cerr << "Add: " << added << "; auto-launch: " << enabled << "\n";
    vr::VR_Shutdown();
    return enabled == vr::VRApplicationError_None ? 0 : 1;
}
```

Save this snippet as `/tmp/register-panel.cpp`, compile against the pinned SDK
headers and installed library, then run it **only when ready to register**:

```sh
c++ -std=c++17 /tmp/register-panel.cpp -I build/_deps/openvr-src/headers \
  -L "$prefix/lib/frameeyeosc-panel" -Wl,-rpath,"$prefix/lib/frameeyeosc-panel" \
  -lopenvr_api -o /tmp/register-panel
/tmp/register-panel "$prefix/share/frameeyeosc-panel/frameeyeosc-panel.vrmanifest"
```

Verify `IsApplicationInstalled("frameeyeosc.panel")` and
`GetApplicationAutoLaunch("frameeyeosc.panel")` via the same SDK, or confirm the
entry under SteamVR startup/overlay settings. To undo registration, call
`SetApplicationAutoLaunch("frameeyeosc.panel", false)` then
`RemoveApplicationManifest(absolute_manifest_path)` and check their results.
No registration, HMD launch, remote SSH action, push or publish was performed
as part of the local implementation checks.

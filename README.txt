bemandi - Direct3D 9 video to NDI

A Windows x64 Spice plugin that captures a displayed game backbuffer and sends
it through the standard NDI High Bandwidth runtime. No OBS process, native game
recorder, NVENC encoder, or NDI Advanced SDK is used by this implementation.

NDI information and runtime: https://ndi.video/for-developers/ndi-sdk/
NDI® is a registered trademark of Vizrt NDI AB.

Current status

Working prototype, locally tested with a synthetic moving D3D9/D3D9Ex source
and a real NDI receiver at 1920x1080 / 120 fps. Includes color/motion checks,
device reset, swapchain Present, render-state checks, no-subscriber behavior,
and slow receiver tests. Actual IIDX gameplay and the i5-4690/GTX1060 cabinet
remain untested. Do not interpret synthetic Present-call timing as game frame
time, end-to-end latency, or a guarantee of no stutter.

One selected video stream is implemented. Audio and simultaneous main/subscreen
streams are not implemented in this version. A separate audio source can be
added later without changing the video pipeline.

How it works

Game backbuffer -> bounded GPU ring -> worker readback -> NDI runtime -> LAN.

Default mode, format=uyvy:
  Copy the backbuffer into a GPU texture, convert RGB to packed BT.709 limited
  range UYVY with a pixel shader, then download half as many bytes as BGRA.
  NDI performs compression and transmission. This is color conversion/chroma
  subsampling, not an extra compression codec before NDI.

Comparison mode, format=bgra:
  One GPU copy followed by BGRA-layout readback. Submit as BGRX because the
  displayed game image is opaque; backbuffer alpha is not broadcast.
  This avoids our shader pass but leaves color conversion to the NDI runtime.

The presentation thread uses a try-lock, reserves one of four slots, submits
GPU work and returns. A full queue causes a capture drop. It does not call NDI,
wait for a GPU completion query, allocate frame buffers, or log each frame.
The worker owns readback and NDI calls. GPU queries gate readback; async NDI
buffer ownership is preserved until the next submission or flush completes.
Capture skips pixels when there is no connected receiver.

This is not a promise of wait-free GPU operation: D3D9 driver calls and
GetRenderTargetData on the multithreaded game device can still contend with
rendering. Measure game frame times on the target. Initial resource allocation
occurs during device creation; reset/shutdown drains the worker before releasing
default-pool resources. Hooks remain installed as pass-through after stop, and
the DLL is retained until process exit for hook-code lifetime safety.

Requirements

- Windows x64, D3D9 or D3D9Ex, 8-bit BGRA/BGRX backbuffer.
- A D3DCREATE_MULTITHREADED device. The plugin refuses other devices and does
  not change the game's device creation flags.
- Installed NDI 6 runtime on the sender; compatible NDI software on the receiver.
- Spice supporting the v0 plugin entry point. Load before game device creation.
- A wired network with enough measured capacity for the selected NDI streams.

Use

1. Copy bemandi.dll and bemandi.ini into the same directory on the cabinet.
2. Add this argument to the existing Spice launch command, retaining its other
   arguments:

       -k "D:\path\to\bemandi.dll"

3. Start the game normally. Look in bemandi.log next to the DLL. Initialization
   and five-second capture counters are written there.
4. On the receiving computer, select the NDI source named IIDX Main in DistroAV
   or another NDI receiver. NDI typically displays the host name as well.

No game DLL modification is required. This repository does not deploy to the
cabinet, edit its launcher, or change firewall rules automatically.

Configuration (restart the process to apply changes)

The default file is config/bemandi.ini. Its [video] keys are:
  format       uyvy (default) or bgra
  fps          capture target, 1..240; default 120
  width        expected backbuffer width, default 1920; must be even
  height       expected backbuffer height, default 1080
  swapchain    selected device swapchain index, default 0
  source_name  NDI source label, default IIDX Main
  ndi_runtime  optional absolute path to Processing.NDI.Lib.x64.dll

Width/height are selection checks, not scaling settings. No scaling is currently
performed. 60 fps is an available comparison, not a necessary game/NDI limit.
No bitrate setting is exposed: the standard NDI runtime controls its codec.
Two receiver connections may change bandwidth and runtime work; benchmark the
actual topology. A single selected swapchain is supported; adapter-group IIDX
and secondary-screen selection need cabinet validation.

If the runtime path is empty, the loader checks NDI_RUNTIME_DIR_V6, then the
installed NDI 6 Runtime and NDI 6 Tools directories beneath Program Files.
It does not search the working directory for an arbitrary DLL.

Build

Install Visual Studio 2022 C++ tools and a Windows SDK, plus Git. CMake 3.25+
is required. Bootstrap can install CMake locally using Python/pip if absent:

    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap.ps1
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1

The bootstrap fetches pinned MinHook, Spice ABI and NDI interface headers.
It does not download/install the proprietary NDI runtime. The NDI headers have
their own MIT notices; runtime usage remains governed by its SDK agreement.

Optional build parameters:
  -SdkRoot        standalone Microsoft Windows SDK NuGet layout, if used
  -SpiceSdkInclude directory containing spicesdk.h
  -NdiInclude     directory containing Processing.NDI.Lib.h

For this workstation's existing standalone Windows SDK:
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -SdkRoot 'D:\Share\chart-requester\reference\sdk'

Artifacts: build/bemandi.dll, build/capture_test.exe, build/ndi_receive_test.exe.
To create a distribution directory including config and dependency notices:
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1

Tests

The build runs core tests for capture cadence, long-uptime timestamp arithmetic,
and exclusive buffer ownership. For actual GPU/NDI integration tests:

    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-video.ps1

This starts hidden synthetic source/receiver processes, does not touch a running
game, and writes machine-readable results under test-output/. The receiver
verifies decoded color bands and changing marker positions, declared resolution
and frame rate, and increasing capture timecodes. The generator checks selected
render states after each Present.

cpu_core_percent is sender process CPU time divided by elapsed time: 100 means
one fully used logical core, not 100 percent of the whole CPU. Tests run source
and receiver on the same PC, without a real 1GbE link or game workload. Process
CPU excludes receiver CPU but shares GPU/memory resources with it. Short tests
include connection startup/reset gaps. These are functional checks and initial
comparisons, not a rigorous cabinet benchmark.

The bytes statistic is raw submitted image payload, NOT compressed network
traffic. sent/completed counts SDK submissions, NOT delivery acknowledgments.
max_capture_us is the maximum observed capture-call duration. Use PresentMon or
equivalent game frame-time measurements for real 120 Hz deadline analysis.

Known limitations and next validation

- Capture begins only for a newly created matching device after plugin startup.
  This is intended for -k startup loading, not arbitrary late injection.
- No resizing/scaling, HDR/10-bit formats, D3D11/12, or audio.
- Device removal/failed reset is not equivalent to the successful reset tests.
- Current tests do not validate real adapter-group/multihead IIDX, live gameplay,
  overlays from another process, physical network congestion, or long sessions.
- If eco mode stops presenting, no new frames are generated; no keepalive image
  is synthesized yet.
- The GPU conversion uses BT.709 4:2:2 limited range. This does not preserve every
  RGB sample losslessly. Verify colored text and game-specific output levels.
- This implementation does not reuse game recorder functions or hardcoded
  bm2dx.dll addresses. Its private reverse-engineering material is ignored by Git.

Open source

Project code is MIT licensed; see LICENSE and THIRD_PARTY_NOTICES.txt.
NDI itself is proprietary. Only the interface headers are fetched for building;
the runtime remains an external dependency. No OBS/DistroAV implementation code
is compiled into this project. Keep game binaries, decompilations, build outputs,
and local SDK installations out of the public source repository.

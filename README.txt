bemandi - Direct3D 9 video and native game audio to NDI

A Windows x64 Spice plugin that captures a displayed game backbuffer and sends
it through the standard NDI High Bandwidth runtime. A separate IIDX Audio source
copies the supported game's native stereo mix. No OBS process, native recording
session, NVENC encoder, AAC encoder, or NDI Advanced SDK is required.

NDI information and runtime: https://ndi.video/for-developers/ndi-sdk/
NDI® is a registered trademark of Vizrt NDI AB.

Current status

Video has been tested on an IIDX34 cabinet (Ryzen 5 3600 / GTX 1660 SUPER) at
1920x1080 / 120 Hz, plus synthetic D3D9/D3D9Ex reset, color, motion, swapchain,
state-restoration, idle and slow-receiver tests. The i5-4690/GTX1060 minimum
configuration remains untested. Present counts are not frame-time percentiles
or a guarantee of no stutter.

Separate stereo audio is implemented and tested with synthetic PCM and live
IIDX34 audio through a real NDI receiver. The cabinet's ASIO run delivered
nonzero stereo PCM at 44100 Hz with no observed queue drops or invalid samples;
the user also confirmed audible output. Full scene coverage, WASAPI output,
native recording coexistence, A/V alignment and long sessions remain untested.
Simultaneous main/subscreen video streams are not implemented.

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

Audio: native stereo mixer -> bounded PCM queue -> worker -> IIDX Audio -> LAN.
The tap calls the game's original audio function first and leaves the native
recorder callback intact. It copies interleaved float samples into preallocated
storage; it does not wait, allocate, log or call NDI on the game audio thread.
No receiver means no PCM copy. A full queue drops audio instead of blocking.
The worker packs 10 ms stereo packets at 44100 Hz, deinterleaves and limits them
to +/-1, and sends PCM to NDI. It does not run AAC or a resampler. Raw float
payload is about 2.82 Mbit/s before runtime/protocol overhead. Queue storage is
about 4 MiB; stale audio over 100 ms old is discarded after a stall.

Both sources use one QPC-based timestamp epoch. Audio follows sample counts
between discontinuities; clock drift over 30 ms triggers a timestamp re-anchor
without backward timecodes. There is no automatic receiver-side A/V alignment.
The tap precedes cabinet master gain/limiting and device conversion, so its level
can differ from the cabinet speakers. It is the main stereo recording mix, not
all six physical ASIO output channels, microphone capture or Windows loopback.

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
- Spice supporting the v0 plugin entry point. Load before video device creation.
  Audio supports a sound graph already initialized before the SDK entry point.
- Native audio requires a verified game profile; see docs/game-updates.txt.
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
5. Add IIDX Audio as a second NDI input for sound. Set its level and sync offset
   on the streaming computer. IIDX Main remains video-only. Disable duplicate
   audio inputs from previous OBS/NDI capture configurations if applicable.

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

Its [audio] keys are:
  enabled      1 (default) for separate native audio, 0 to disable
  source_name  distinct NDI label, default IIDX Audio
Audio shares the runtime path configured under [video]. A missing [audio]
section uses these defaults. Unknown game hashes or altered hook entry bytes
disable audio and log the reason; video can continue independently.

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

Artifacts: build/bemandi.dll, build/capture_test.exe, build/ndi_receive_test.exe.
To create a distribution directory including config and dependency notices:
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1

Tests

The build tests capture cadence, timestamp arithmetic, audio queue overflow and
concurrent ownership, exact profile selection, and D3D9 slots against the SDK.
For GPU/NDI and audio/NDI integration tests (installed NDI runtime required):

    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-video.ps1
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-audio.ps1

This starts hidden synthetic source/receiver processes, does not touch a running
game, and writes machine-readable results under test-output/. The receiver
verifies decoded color bands and changing marker positions, declared resolution
and frame rate, and increasing capture timecodes. The generator checks selected
render states after each Present.
The audio test uses varying 16..1024-frame chunks and checks stereo sample data,
44100 Hz / 441-sample packets, levels, monotonic timestamps and sender restart.
To inspect a live audio source without changing device routing:
    build\ndi_audio_probe.exe "IIDX Audio" 10
It reports packets, sample rate/channels, peak, RMS and nonzero samples. A silent
scene can have valid packets with zero level. No packets means source capture,
receiver connection or transport needs investigation. The cabinet log reports
receiver count and tap callbacks separately from captured samples/NDI packets.

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
- No resizing/scaling, HDR/10-bit formats or D3D11/12.
- Device removal/failed reset is not equivalent to the successful reset tests.
- Simultaneous multihead capture, overlays from another process, physical
  network congestion and long sessions require further validation.
- If eco mode stops presenting, no new frames are generated; no keepalive image
  is synthesized yet.
- The GPU conversion uses BT.709 4:2:2 limited range. This does not preserve every
  RGB sample losslessly. Verify colored text and game-specific output levels.
- Audio needs a build-specific header; video uses only standard D3D9 interfaces.
  See docs/game-updates.txt for the update workflow. Private binaries and
  decompilations are ignored by Git and are not required to build the project.
- No synthetic silence is sent when the game's audio graph stops being pulled.
  Scene coverage and native recording coexistence still need cabinet testing.

Open source

Project code is MIT licensed; see LICENSE and THIRD_PARTY_NOTICES.txt.
NDI itself is proprietary. Only the interface headers are fetched for building;
the runtime remains an external dependency. No OBS/DistroAV implementation code
is compiled into this project. Keep game binaries, decompilations, build outputs,
and local SDK installations out of the public source repository.

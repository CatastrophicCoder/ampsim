# Windows: the standalone and the VST3

A plan, not a record of work done. Nothing here has been built or compiled on Windows yet. The
first step below is the one that turns the guesses into facts.

The scope is the two formats Windows can have — the standalone and the VST3. The AU is Apple-only.
JUCE drops a format the platform does not support from `FORMATS`, so the `juce_add_plugin` call can
stay as it is. Making that explicit with a platform check would be more honest for whoever reads
it.

## What the first CI build found

MSVC 19.51 on `windows-latest` built the standalone, the VST3 and the tests with no source
changes and none of the flags below. The only warnings were C4458, where a local shadows a JUCE
member (`playHead`, `text`), and one int-to-float conversion. 122 of 123 tests passed.

The failure was a real bug on every platform. `ModelResampler::prepare` divided by a host rate of
zero when a model was prepared before the device was open, and cast the resulting NaN to `int`.
That is undefined behaviour. Apple silicon happens to give 0, so it never showed. x64 gives
`INT_MIN`, which became a "vector too long" exception. This is fixed, and the fix has a resampler
test that fails on macOS too. With the fix in, all 124 pass on Windows.

The first pluginval run on Windows only looked like a pass. PowerShell does not wait for a
GUI-subsystem program, and `pluginval.exe` is one, so the step returned while pluginval was still
in the editor tests and reported success without a result. The step now runs under bash. Under bash, pluginval ran for
eighteen seconds, completed every group at strictness 10 and reported `SUCCESS`. The groups
include the editor and its automation, parameter fuzzing, thread safety and the bus layouts. Its
VST3 validator stage is skipped, as it is on macOS, because no validator path is set.

### What it costs on the runner

`ampsim_bench` on `windows-latest` (x64, MSVC, default `/arch`, so SSE2), against the same
benchmark on an M-series Mac. Percentages are of one core:

| | 64 samples | 512 samples |
| --- | --- | --- |
| 48 kHz, amp + cab, Windows runner | 12.9 % | 10.6 % |
| 48 kHz, amp + cab, Mac | 3.5 % | 2.8 % |
| 44.1 kHz, amp + cab, Windows runner | 17.1 % | 15.0 % |
| 44.1 kHz, amp + cab, Mac | 5.7 % | 4.5 % |
| 44.1 kHz, everything on, Windows runner | 17.7 % | 15.4 % |

What this establishes:

- **It keeps up with a wide margin.** The worst case, everything on at 44.1 kHz and 64 samples, is
  under a fifth of one core.
- **The shape is the same as on the Mac.** The model is the cost; every pedal together adds well
  under one point, and a bypassed plugin is under 0.15 %.
- **The resampler costs about twice as much in absolute terms**: roughly four points at 44.1 kHz,
  against two on the Mac. That is the same proportion of the total, so it does not point at the
  resampler in particular.
- **Overall the runner is three to four times the Mac.** The table alone cannot say why.

The figures above also show that one run cannot be compared with another. The next run's default
build measured 10.1 % where this one measured 12.9 %, because each run gets whatever machine is
free. That one was an AMD EPYC 9V74 (Zen 4).

### SSE2 against AVX2, on the same machine

The benchmark was built a second time with `/arch:AVX2`, which lets Eigen use AVX2 and FMA. Both
builds then ran in one job, interleaved as default, AVX2, AVX2, default:

| amp + cab | default (SSE2) | `/arch:AVX2` | change |
| --- | --- | --- | --- |
| 48 kHz, 64 samples | 10.06 % | 6.78 % | −33 % |
| 48 kHz, 512 samples | 8.70 % | 5.38 % | −38 % |
| 44.1 kHz, 64 samples | 13.65 % | 10.00 % | −27 % |
| 44.1 kHz, 512 samples | 12.34 % | 8.64 % | −30 % |

- **The model gains and the resampler barely does.** The cost the resampler adds (the 44.1 kHz
  figure minus the 48 kHz one) is 3.6 points under SSE2 and 3.2 under AVX2. JUCE's windowed-sinc
  interpolator is not vectorised the way Eigen is. So at 44.1 kHz with AVX2 the resampler is about
  a third of the total, against about a quarter at 48 kHz.
- **About half the gap to the Mac was the instruction set.** With AVX2 the runner is 1.8 to 1.9
  times the Mac's cost, against three to four times without it. The rest is the machine, the
  compiler, or both. Separating those would take a clang-cl build measured the same way.
- **The pedals are unaffected either way**, and still add under a point together.

What it means for the architecture decision below: an AVX2 build is measurably cheaper. The
price is a CPU requirement. Every Intel Core since Haswell (2013) and every AMD Zen (2017) has
AVX2. Some Pentium and Celeron parts lacked it for years after that. A plugin built for AVX2 and
loaded on a CPU without it does not refuse politely: it faults on the first such instruction,
which takes the host down with it.

A clean build takes about twelve minutes on the runner. Six of those are spent linking the VST3
and the standalone, which is link-time code generation, not compiling.

## What already ports without change

A survey of the source for anything tied to macOS found less than expected:

- **The DSP is plain C++17 on `juce::dsp`.** No intrinsics, no POSIX calls, no `M_PI`, no
  Objective-C. The chain, the tuner, the transpose and the metronome have nothing to port.
- **NAM Core supports Windows.** Its own `CMakeLists.txt` has a Windows branch, and NAM's plugin
  ships on Windows. Eigen builds under MSVC.
- **`$<LINK_LIBRARY:WHOLE_ARCHIVE,...>` works on MSVC.** CMake 3.24 maps it to `/WHOLEARCHIVE`,
  so NAM's file-scope architecture registration keeps working.
- **The data directories are already guarded.** `BundledAssets.cpp` and `PresetManager.cpp` only
  append `Application Support` under `JUCE_MAC`. Elsewhere, `userApplicationDataDirectory` is
  `%APPDATA%`, so presets and the bundled model land in `%APPDATA%\AmpSim\`.
- **The panel uses no image assets.** The icon PNGs become an `.ico` through JUCE.
- **MIDI learn in the standalone** goes through `StandalonePluginHolder`, which works the same way
  on Windows.

## What has to change before it builds or works properly

In the order the first build is likely to find them:

1. **Model paths with non-ASCII characters will fail to open.** `ModelLoader.cpp:78` builds a
   `std::filesystem::path` from `getFullPathName().toStdString()`. That string is UTF-8, but on
   Windows a narrow string passed to `path` is read in the ANSI code page. A file under
   `C:\Users\Jörg\` or in a folder named `Äänet` would report "file not found". macOS hides this
   because there narrow strings *are* UTF-8. The fix: build the path from
   `toWideCharPointer()` under `_WIN32`. It should get a test that loads a model from a directory
   with a non-ASCII name, which then also guards macOS.
2. **Compiler flags NAM's own build would have set.** We build `nam_core` ourselves rather than
   through NAM's CMake, so `NOMINMAX` and `WIN32_LEAN_AND_MEAN` (which its Windows branch adds) are
   missing. Eigen-heavy translation units may also need `/bigobj`. Two sources (`PluginEditor.cpp`,
   `ModelResampler.h`) contain non-ASCII characters in comments. MSVC warns about those under a
   non-UTF-8 code page, so add `/utf-8`. All of these go behind `if(MSVC)`.
3. **The plugin copy after a Release build would fail.** JUCE's default VST3 destination on Windows
   is `C:\Program Files\Common Files\VST3`, which needs administrator rights. There are two options:
   set `VST3_COPY_DIR` to the per-user location `%LOCALAPPDATA%\Programs\Common\VST3` (which VST3
   hosts scan), or keep `AMPSIM_COPY_PLUGIN` off on Windows and install only through the
   installer. The Release-only reason for copying (keeping a Debug build out of the host) still
   applies.
4. **The fonts are not there.** `AmpLookAndFeel::font` asks for Avenir Next and `stencil` for
   Futura. Both ship with macOS, and neither is on Windows, so JUCE falls back to the default sans
   face. Every page lays out in fixed logical points, and different text metrics are where clipped
   captions and labels in the wrong place come from. This needs a decision; see below.
5. **The microphone privacy switch.** Windows 10 and 11 have *Settings → Privacy → Microphone →
   Let desktop apps access your microphone*. Unlike macOS, it needs nothing in the binary. But when
   it is off, the result is the same: the device appears and silence arrives. That belongs in the
   guide. There is nothing to fix in code.

Smaller things, none of which block a first build:

- `tools/HostCheck.cpp` hard-codes the `~/Library/Audio/Plug-Ins` paths and hosts the AU. It is a
  developer tool and can stay macOS-only, or take its paths from the platform.
- A saved session stores absolute model and IR paths. A session made on a Mac and opened on
  Windows names files that do not exist there, including the bundled ones. The same thing happens
  between two Macs with different user names, so this is not Windows-specific. It becomes more
  visible once a project can move between systems.
- The CI release notes say "for Macs with Apple silicon". `packaging/README.md`, the guide's
  install section and the `CLAUDE.md` line that lists Windows as deliberately out of scope all need
  updating once this is agreed.

## Decisions that are not technical

Each is a choice between options with real costs on both sides. They are set out here to be
decided, not pre-decided.

### Fonts

| Option | For | Against |
| --- | --- | --- |
| Let Windows fall back to its default face | No work, no licensing question | Segoe UI is the "system font every other plugin defaults to" that `AmpLookAndFeel.h` says it chose Avenir to avoid. Metrics differ, so every caption needs checking at every scale |
| Name a Windows face per platform (e.g. Segoe UI Variable, Bahnschrift) | Ships with Windows 10/11, nothing to embed | Two typographic identities. The panel has to be rendered and checked on both |
| Embed open-licensed fonts on every platform (e.g. an OFL geometric face for the stencil, a humanist sans for the interface) | One look everywhere; the snapshot harness checks it once | Changes the Mac panel too, which was settled in the rework. Adds BinaryData. The OFL is permissive, but each font's licence still needs reading |

Avenir Next and Futura are commercial Linotype faces. Embedding them is not an option.

### Low-latency audio in the standalone

A JUCE standalone on Windows offers Windows Audio (WASAPI shared), Windows Audio exclusive mode,
low-latency mode, and DirectSound by default. ASIO is what most guitar interfaces' own drivers
provide, and it is what a guitarist on Windows will look for.

| Option | For | Against |
| --- | --- | --- |
| WASAPI only | No extra SDK. Exclusive mode gets close to ASIO latency on many interfaces | Many interface vendors' best path is their ASIO driver. Exclusive mode takes the device away from other apps |
| Also enable ASIO (`JUCE_ASIO=1` plus the Steinberg ASIO SDK) | The driver guitarists expect. Lowest latency on most USB interfaces | The SDK is a separate download, kept out of the repo or added as a pinned dependency. **Its licence terms need checking before it is added**, under the same rule as any other dependency |

This only affects the standalone. The VST3 uses whatever driver its host does.

### Signing and the first-launch warning

An unsigned Windows installer or executable gets SmartScreen's "Windows protected your PC". It
can be dismissed with **More info → Run anyway**, much like Gatekeeper's *Open Anyway*. Windows
has no ad-hoc signature to fall back on, so the choice is between signing and not signing.

| Option | For | Against |
| --- | --- | --- |
| Unsigned, with the bypass documented | Free; matches the current macOS position | Every user sees the warning. Some managed machines block unsigned installers outright |
| Microsoft's Trusted Signing (Azure Artifact Signing) | Low monthly cost; integrates with CI | Eligibility for individuals depends on country and identity verification; check before planning around it |
| A conventional OV or EV code-signing certificate | Works anywhere | Annual cost. Since 2023 these come on hardware tokens or cloud HSMs, which complicates signing in CI |

### Installer format

| Option | For | Against |
| --- | --- | --- |
| A `.zip` with the `.vst3` bundle and the `.exe` | Trivial to produce | The user has to know where VST3s go |
| Inno Setup | Free, scriptable, and on GitHub's Windows runner images. Can offer per-user or all-users installs | One more script to maintain beside `package.sh` |
| WiX / MSI | What enterprise deployment expects | The most work, for an audience this project does not obviously have |

### Architecture

x64 is the default target and covers nearly all Windows audio machines. Windows on ARM (Snapdragon
laptops) can run x64 plugins only inside an x64 host, and native ARM64 hosts need ARM64 plugins.
Adding ARM64 later is a second CI matrix entry.

Eigen on x64 defaults to SSE2. `/arch:AVX2` measured 27 to 38 % cheaper (see the benchmark above).
Making the AMD Zen and Intel Haswell generation the minimum is the cost of that. The options:

| Option | For | Against |
| --- | --- | --- |
| SSE2 only (the default) | Runs on any x64 CPU | The higher cost measured above |
| AVX2 only | The lower cost, one build | On a CPU without AVX2 it crashes the host rather than failing to load. The installer could check and refuse |
| Both, chosen by the installer | Each machine gets the build it can run | Two builds to test and ship, and an installer that inspects the CPU |
| Runtime dispatch inside one binary | One build that uses AVX2 where it exists | NAM and Eigen compiled twice into separate namespaces and picked by `cpuid`. The most work, and it reaches into a dependency |

## Proposed order of work

1. **A Windows CI job that only configures, builds and runs `ctest`.** It goes on a branch, using
   `windows-latest` with MSVC — either the Visual Studio generator, or Ninja after a
   developer-prompt action. It does not package or publish. This is the cheapest way to find out
   which of the items above actually bite. It also needs no Windows machine, which matters because
   this repository is developed on Apple silicon.
2. **Fix what that build finds**, starting with the model-path encoding and the MSVC flags. Each
   gets a test where it can have one.
3. **Validate the VST3 with pluginval in that job.** pluginval publishes a Windows binary; it is
   the same check the macOS job already runs.
4. **Run the benchmark (`ampsim_bench`) on the Windows runner.** It is the first real number for
   NAM on x64 under MSVC. A CI runner is noisy, so treat the figure as an order of magnitude, not a
   specification.
5. **Settle the fonts.** Render the editor with the snapshot harness on Windows and look at it, as
   `CLAUDE.md` asks for any panel change.
6. **Try it by hand.** A Windows 11 ARM virtual machine on the Mac (Parallels, VMware Fusion or
   UTM) runs the x64 builds under emulation. That is enough to check the panel, presets, MIDI learn
   and that a VST3 host such as REAPER loads the plugin. It says nothing about latency or CPU, which
   need a real Windows PC and an audio interface.
7. **Packaging and release.** Pick the installer format and the signing position, add a Windows
   artefact to the tagged release, and update the guide, the packaging README, the changelog and
   `CLAUDE.md`.

Steps 1 to 4 are mechanical and can be done without a decision. Step 5 onward waits on the choices
above.

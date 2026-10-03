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

### NAM's own build options, measured first on the Mac

The bundled capture is a `SlimmableContainer` of two WaveNets, with 3 and 8 channels. Both match
NAM's A2 shape, so `NAM_ENABLE_A2_FAST` does take effect: the benchmark now prints whether it
did. Our build compiled that path out until now, because NAM's own CMake is what defines the
macro and we do not use it. Each build was compared with the normal one in the same session,
interleaved twice, on an M-series Mac at 48 kHz, amp + cab:

| | 64 samples | 512 samples |
| --- | --- | --- |
| normal build | 4.2 % | 2.96 % |
| `NAM_ENABLE_A2_FAST` | 3.95 % (−7 %) | 2.87 % (−3 %) |
| `NAM_USE_INLINE_GEMM` | 4.22 % (±0) | 3.46 % (+17 %) |

### The same options, and two more, on the Windows runner

Five builds ran in one job on an AMD EPYC 9V74, 48 kHz in one order and 44.1 kHz in the reverse.
Each differs from the normal build (MSVC, AVX2) in one thing. Amp + cab, percent of one core:

| | 48 kHz, 64 | 48 kHz, 512 | 44.1 kHz, 64 | 44.1 kHz, 512 |
| --- | --- | --- | --- | --- |
| normal | 7.34 | 5.63 | 10.19 | 8.76 |
| `NAM_ENABLE_A2_FAST` (taken for both submodels) | 6.89 | 5.95 | 9.44 | 7.99 |
| `NAM_USE_INLINE_GEMM` | 5.85 | 6.35 | 9.21 | 8.33 |
| clang-cl 20.1.8 with lld-link | 7.01 | 5.01 | 9.99 | 9.61 |
| NAM with `/GL`, every link `/LTCG` | 12.54 | 10.58 | 15.72 | 15.70 |

**This run was noisy, and that limits what it can show.** In several builds the "everything on"
row came out *cheaper* than "amp + cab". It does strictly more work, and in earlier runs it never
did. So the runner's noise this time was about a point, roughly fifteen percent, and anything
smaller than that is not a result:

- **Whole-program optimisation of NAM is clearly worse**: 1.5 to 1.8 times the cost, at every
  block size and both rates, far outside the noise. The cause is not known. Whatever it is, it is
  not worth pursuing.
- **A2, inline GEMM and clang-cl are within the noise.** A2 was lower at 44.1 kHz in both
  columns and mixed at 48 kHz. That is the same direction as the Mac's cleaner measurement, but it
  does not confirm it. Inline GEMM and clang-cl move both ways.
- clang-cl built and ran without changes beyond the linker and archiver, so it is an option if a
  later measurement favours it.

Resolving differences this small would need many more interleaved repetitions per build than
one pass at each rate, or a quiet machine of one's own.

**Decided: the A2 fast path is on, everywhere, and the other three are not.** `NAM_ENABLE_A2_FAST`
is a public definition of `nam_core`. It is a second implementation of the same model, so
`tests/ModelTests.cpp` builds each of the bundled capture's submodels both ways, from the same
weights, and null-tests one against the other. They agree to −122 and −132 dB. The threshold is
−100 dB, which a single weight 1 % out (−77 dB) fails. The test also passes on the
Windows runner (MSVC, AVX2), and the benchmark there reports the path taken for both submodels. The experiment steps are gone from CI;
the benchmark still prints what it was built with and whether the A2 path was taken.

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
   captions and labels in the wrong place come from. **Done:** see Fonts below.
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

**Decided: embed Figtree for the interface and Jost for printed names, on every platform.** Both
are under the SIL Open Font License with no Reserved Font Name, which matters because the files
in `resources/fonts` are static Regular and Medium instances cut from the variable fonts in
google/fonts, and the OFL counts that as a modified version. The licence texts sit beside them.
The instances keep the copyright and licence in their name tables, which the OFL accepts as the
notice that has to travel with each copy. The installer should still ship the two licence texts.

- **Every height on the panel was chosen for the old faces, so each new face is scaled to put its
  capitals where theirs were.** A JUCE font height is the face's ascent plus descent, a different
  share of the letters in every face. Measured from the files: Figtree's cap height is 0.583 of
  that span against Avenir Next's 0.518, and Jost's 0.484 against Futura's 0.586. At those scales
  Figtree's widths land within one per cent of Avenir's. Jost sets about six per cent wider than
  Futura, which still fits everywhere it is used. Unscaled, the tempo reading was cut off as
  "120 BP…" and every name on an object shrank.
- **Figtree is also the default sans-serif for the look-and-feel**, so what JUCE draws itself —
  popup menus, the slider value bubble — no longer falls back to the system font. That is a
  change on the Mac too.
- `tools/PanelSnapshot.cpp` renders each page to a PNG. It is what these were checked with, and
  the Windows CI job uploads its output so the Windows panel can be looked at.
- **Checked on Windows.** The CI renders of all three pages, at 1.5x on the runner, match the Mac
  renders: the same faces, nothing cut off, and every caption and reading in the same place. The
  only differences are in antialiasing.

### Low-latency audio in the standalone

**Decided after step 6: ASIO is enabled.** See *Tried by hand* above.

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

**Decided: Inno Setup.** The plan for it is under *Packaging with Inno Setup* below.

### Architecture

x64 is the default target and covers nearly all Windows audio machines. Windows on ARM (Snapdragon
laptops) can run x64 plugins only inside an x64 host, and native ARM64 hosts need ARM64 plugins.
Adding ARM64 later is a second CI matrix entry.

Eigen on x64 defaults to SSE2. `/arch:AVX2` measured 27 to 38 % cheaper (see the benchmark above).
Making the AMD Zen and Intel Haswell generation the minimum is the cost of that.

**Decided: AVX2 only.** `CMakeLists.txt` sets `/arch:AVX2` for every MSVC-style build, and the CI
benchmark step fails if the flag goes missing. Because a CPU without AVX2 crashes the host rather
than failing to load, the installer (step 7) should check for AVX2 and refuse to install without
it. The guide should state the requirement too. The options that were weighed:

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

## Tried by hand: step 6

On a laptop with an Intel i5-8350U (a 15 W part with AVX2) and an Audient iD4 mkII, from the CI
installer:

- **The installer, the standalone and the VST3 install, and REAPER opens the plug-in.** REAPER
  then reported the audio device closed and gave no sound. That lies between REAPER and the
  interface, not in the plug-in, and the likely cause is two programs contending for it.
- **The standalone in *Windows Audio (Low Latency Mode)* was stuck at 44.1 kHz and 441 samples
  (10 ms)**, and felt laggy. Shared mode takes its rate from the device's format in Windows'
  Sound settings and its period from the audio engine. Low Latency Mode only shortens the
  period where the driver supports it, which this one does not. The 44.1 kHz also puts the
  resampler in the chain.
- **In *Windows Audio (Exclusive Mode)* the buffer went down to 132 samples.** That felt better
  at first, until the output became corrupted.

**That corruption was a real bug, on every platform: a block larger than `prepareToPlay`
announced overran the chain's buffers.** JUCE's documentation says hosts may exceed the
announced size. A test that announces 64 samples and then sends 1024 crashed with a segmentation
fault, after an assertion in the drive pedal's oversampler. `processBlock` now takes an
oversized block in pieces that fit, and the metronome offsets the host's position for each
piece. Whether the exclusive-mode driver really sent oversized blocks is not proven. The
laptop's CPU missing deadlines at 132 samples would sound similar, and nothing has been measured
on it yet.

What would give a guitar latency on this interface is ASIO. Audient's own Windows driver
provides it, and the standalone could not use it while `JUCE_ASIO` was off.

**Decided: ASIO is on in the Windows build** (`JUCE_ASIO=1` in `CMakeLists.txt`). The headers are
the copies of Steinberg's SDK bundled with JUCE 9, under the SDK's GPLv3 option, which is
compatible with the AGPLv3. The installer ships the SDK's licence as `LICENSE-ASIO-SDK.txt`.
The standalone still starts on JUCE's first device type, Windows Audio; ASIO is chosen once in
*Options* and remembered.

**Through ASIO in REAPER, on the same laptop, it is fine.** Audient's own driver at 128 samples
reported 4.1 ms in and 5.5 ms out, about 10 ms round trip, and REAPER's performance meter showed
AmpSim at about 3.7 % of the CPU while playing. That is in line with the Mac's 3 to 4 %, so the
lag in the standalone was Windows' shared audio path, not processing cost. 128 samples was the
smallest buffer that played cleanly on this machine.

## Packaging with Inno Setup

The plan for step 7. The installer script, its build script and CI's install test exist; the release job does not. It mirrors `packaging/package.sh` where the two
platforms ask the same question, and says so where they differ.

### What it produces

`AmpSim-<version>-windows-x64-setup.exe`: one installer, built by `ISCC.exe` from a script at
`packaging/windows/AmpSim.iss`. A `packaging/package-windows.ps1` drives it, the counterpart of
`package.sh`: build Release, stage, run ISCC, write to `build/artefacts`. The version comes out
of `CMakeLists.txt` with the same pattern `package.sh` and the release step use. It is passed to
ISCC as `/DAppVersion=…`, so the installer, the file name and the tag cannot disagree. The
`windows-latest` image ships Inno Setup 6.7.1, so CI installs nothing.

### What it installs

| Component | Where (all users) | Where (current user) | Selectable |
| --- | --- | --- | --- |
| VST3 | `{commoncf64}\VST3\AmpSim.vst3`, i.e. `C:\Program Files\Common Files\VST3` | `{localappdata}\Programs\Common\VST3\AmpSim.vst3` | yes |
| Standalone | `{autopf}\AmpSim\AmpSim.exe`, with a Start menu entry | the same under `{autopf}`, which Inno maps to the user's own programs folder | yes |
| Licences | `{app}\licenses\`: `LICENSE` (AGPL), `OFL-Figtree.txt`, `OFL-Jost.txt` | the same | no |

Both components are on by default and each can be unticked. That is the same split the macOS
`.pkg` offers, for the same reason: plenty of people want the plugin without the standalone.

The VST3 is copied as the whole bundle directory (`Contents\x86_64-win\AmpSim.vst3`,
`Contents\Resources\moduleinfo.json`), never as the inner file alone. VST3 hosts look for the
bundle.

The bundled amp model and cab IR are **not** the installer's business. The plugin writes them to
`%APPDATA%\AmpSim\Bundled` on first run, as it does on macOS, and presets go beside them.

### Fixed points, not decisions

- **One `AppId` GUID, generated once and never changed.** Inno recognises an upgrade by it, so a
  new version installs over the old one in place. Changing it later makes two entries in
  *Installed apps*, the same mistake as changing a parameter ID.
- **`ArchitecturesAllowed=x64compatible` and `ArchitecturesInstallIn64BitMode=x64compatible`.**
  The build is x64 only. On Windows on ARM this installs the x64 build, which runs only in an x64
  host.
- **`CloseApplications=yes`.** A running standalone, or a host with the VST3 loaded, would
  otherwise leave the old DLL locked and the upgrade half done.
- **Uninstalling leaves `%APPDATA%\AmpSim` alone.** Presets are the user's work. That is also
  what the macOS uninstall instructions do.
- **The installer links to the source.** The AGPL obliges anyone distributing a binary to offer
  the corresponding source. `AppURL` and the finished page point at the GitHub repository and the
  tag, and `LICENSE` is the licence page the installer shows first.

### What has to be done in the code first

1. **The C++ runtime.** JUCE leaves CMake's default, the DLL runtime (`/MD`), so the standalone
   and the VST3 need `VCRUNTIME140.dll` and its siblings. Many Windows machines have them from
   other software, but not all, and a plugin that cannot find them fails to load in the host with
   no useful message. There are two ways out:

   | Option | For | Against |
   | --- | --- | --- |
   | Link the runtime statically: `CMAKE_MSVC_RUNTIME_LIBRARY` set to `MultiThreaded` | Nothing to install alongside; the usual choice for audio plugins; one line in `CMakeLists.txt` | Each binary carries its own copy, about a few hundred KB more. Every target must agree, NAM and Catch2 included, which a global setting gives |
   | Ship and run `vc_redist.x64.exe` from the installer | The standard Microsoft route | The installer needs administrator rights for it even on a per-user install. Larger download. The redistributable has its own licence terms to follow |

2. **The AVX2 check.** The build needs AVX2, and without it the plugin crashes the host, so the
   installer must refuse first. Inno's Pascal script can call `IsProcessorFeaturePresent` from
   `kernel32.dll` with `PF_AVX2_INSTRUCTIONS_AVAILABLE` (40) in `InitializeSetup`, and stop with a
   message that names the requirement. **To verify:** which Windows 10 builds answer that feature
   number at all. An older build that returns false on an AVX2 CPU would refuse a machine that
   could run the plugin. If that turns out to matter, the fallback is a tiny helper executable,
   built with the project, that asks `cpuid` and `xgetbv` directly and is run from the script.
   Windows on ARM emulates x64, and whether its emulator reports AVX2 to an x64 process is a
   second thing to check there.

### Decided since

- **The C++ runtime is linked statically** (`CMAKE_MSVC_RUNTIME_LIBRARY` in `CMakeLists.txt`).
  `package-windows.ps1` runs `dumpbin /dependents` on both binaries and refuses to package one
  that imports `VCRUNTIME`, `MSVCP` or the UCRT.
- **The installer asks who to install for** (`PrivilegesRequired=lowest` with
  `PrivilegesRequiredOverridesAllowed=dialog`). For one user is the default, because it needs no
  administrator. CI installs both ways.
- **The AVX2 check is the API, with a minimum Windows version.** Microsoft documents
  `PF_AVX2_INSTRUCTIONS_AVAILABLE` as answered from Windows 10 version 2004 (build 19041); before
  that it returns zero whatever the processor has. So `MinVersion=10.0.19041`, and no helper
  executable is needed. Windows 10 releases before 2004 are long out of support.
- **The desktop shortcut is an unticked checkbox**, Inno Setup's usual form. It is shown only
  when the standalone is being installed.

**First CI run of all of it: passed.** The whole build links with the static runtime, `dumpbin`
finds no runtime DLL in either binary, ISCC built `AmpSim-0.3.0-windows-x64-setup.exe`, and the
install, the upgrade over it, and the uninstall all passed for all users and for the current
user. Still untested anywhere: the AVX2 refusal (every runner has AVX2), the interactive
who-for dialog (CI passes `/ALLUSERS` and `/CURRENTUSER` instead), and whether a host finds the
per-user VST3 folder.

These were chosen at the time:
| --- | --- |
| ~~Who it installs for~~ | **All users only** (needs administrator rights; one VST3 location every host scans). **Per-user only** (no prompt; uses the per-user VST3 folder, which hosts that follow the VST3 spec scan, but some older hosts may not). **Ask** (`PrivilegesRequiredOverridesAllowed=dialog`; both paths have to be tested) |
| Signing | Unchanged from *Signing and the first-launch warning* above. Inno's `SignTool` directive signs the installer and the uninstaller; the `.vst3` and `.exe` inside are signed separately, before ISCC runs. Unsigned works, with SmartScreen's warning on first run |
| ~~A desktop shortcut for the standalone~~ | Off, on, or offered as a checkbox |

### The release workflow

At present the macOS job publishes the GitHub Release itself. With two platforms that has to
move:

- The Windows job runs `package-windows.ps1` and uploads the installer as an artefact, on every
  run, as the macOS job does with its `.pkg` and `.dmg`.
- A third job, `release`, runs on `v*` tags only. It needs both jobs, downloads both artefacts,
  checks the tag against `CMakeLists.txt` once, and creates the release with all three files.
  Moving the check and the publish there means a tag cannot publish one platform when the other
  failed.
- The release notes stop saying "for Macs with Apple silicon". They name both platforms and
  their requirements: macOS 11 on Apple silicon, Windows 10 or 11 on x64 with an AVX2 CPU.

### Documentation that changes with it

- `packaging/README.md` gains a Windows half: SmartScreen's *More info → Run anyway* if unsigned,
  the AVX2 requirement, where the two components went, and the Windows microphone privacy switch.
- The user guide's install section, and its system requirements.
- `CHANGELOG.md`: Windows support, the font change on both platforms, and the resampler fix.
- `CLAUDE.md`: the scope line that lists Windows builds as deliberately out, the CI and release
  description, and the packaging paragraph.

### Order

1. Settle the C++ runtime, and check the CI artefact on a Windows machine without the
   redistributable (step 6 can do both).
2. Settle who it installs for.
3. Write `AmpSim.iss` and `package-windows.ps1`, with the AVX2 check, and build the installer in
   CI as an artefact.
4. Install, upgrade and uninstall it by hand in a VM. Check that REAPER finds the VST3 at
   whichever location was chosen, and that the AVX2 check behaves.
5. Split the release out into its own job, then tag.

; The Windows installer, compiled by Inno Setup 6's ISCC. Run packaging/package-windows.ps1
; rather than this directly: it builds Release, stages the binaries and passes the defines below,
; the version among them, read out of CMakeLists.txt the way the macOS packaging reads it.
;
; It asks at the start whether to install for everyone (administrator rights, the system VST3
; folder) or for the current user only (no prompt, the per-user VST3 folder). docs/windows.md has
; the reasoning behind each choice here.

#ifndef AppVersion
  #error AppVersion is not defined: run packaging/package-windows.ps1
#endif
#ifndef RepoRoot
  #error RepoRoot is not defined: run packaging/package-windows.ps1
#endif
#ifndef StageDir
  #error StageDir is not defined: run packaging/package-windows.ps1
#endif
#ifndef OutputDir
  #error OutputDir is not defined: run packaging/package-windows.ps1
#endif

#define RepoURL "https://github.com/CatastrophicCoder/ampsim"

[Setup]
; Never change AppId. Inno recognises an upgrade by it, so a new version installs over the old one;
; a new id would put two AmpSims in Installed apps, the installer's equivalent of renaming a
; parameter ID.
AppId={{6EA8C743-B761-4524-BEDA-A20BAA128F6B}
AppName=AmpSim
AppVersion={#AppVersion}
AppVerName=AmpSim {#AppVersion}
AppPublisher=Catastrophic Audio
AppPublisherURL={#RepoURL}
AppSupportURL={#RepoURL}/issues
AppUpdatesURL={#RepoURL}/releases
VersionInfoVersion={#AppVersion}

; Asks: everyone, or just me. Per-user is the default because it needs no administrator rights.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

; {autopf} is Program Files for everyone and %LOCALAPPDATA%\Programs for one user.
DefaultDirName={autopf}\AmpSim
DisableProgramGroupPage=yes

; The build is x64 only. On Windows on ARM this installs the x64 build, which loads only in an x64
; host. 10.0.19041 is Windows 10 2004: the first version whose IsProcessorFeaturePresent answers
; for AVX2 at all, which the check in [Code] depends on.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041

; A running standalone, or a host with the plugin loaded, holds the old binary open.
CloseApplications=yes
RestartApplications=no

; The AGPL is the licence the user agrees to, and the one that obliges an offer of the source.
LicenseFile={#RepoRoot}\LICENSE
#ifdef IconFile
SetupIconFile={#IconFile}
#endif
UninstallDisplayIcon={uninstallexe}
WizardStyle=modern

OutputDir={#OutputDir}
OutputBaseFilename=AmpSim-{#AppVersion}-windows-x64-setup
Compression=lzma2/max
SolidCompression=yes

; Not signed yet, so SmartScreen warns on first run; packaging/README.md says how to get past it.
; Signing goes here as a SignTool= line once a certificate exists, and the .vst3 and .exe have to be
; signed before ISCC runs, in package-windows.ps1.

[Types]
Name: "full";   Description: "The plug-in and the standalone application"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
; The same split as the macOS installer: plenty of people want the plugin and not the standalone.
Name: "vst3";       Description: "VST3 plug-in";           Types: full custom
Name: "standalone"; Description: "Standalone application"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut for the standalone"; Components: standalone; Flags: unchecked

[InstallDelete]
; The bundle is replaced whole on upgrade, so nothing an older version shipped is left inside it.
Type: filesandordirs; Name: "{code:Vst3Dir}\AmpSim.vst3"; Components: vst3

[Files]
; A VST3 is a folder, and hosts look for the folder: copy all of it, never the inner DLL alone.
Source: "{#StageDir}\VST3\AmpSim.vst3\*"; DestDir: "{code:Vst3Dir}\AmpSim.vst3"; Components: vst3; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#StageDir}\Standalone\AmpSim.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

; Always installed. The two fonts are embedded in both binaries, and the OFL asks for its text to
; travel with them.
Source: "{#RepoRoot}\LICENSE"; DestDir: "{app}\licenses"; DestName: "LICENSE-AmpSim.txt"; Flags: ignoreversion
Source: "{#RepoRoot}\resources\fonts\OFL-Figtree.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#RepoRoot}\resources\fonts\OFL-Jost.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
; The standalone's ASIO support is built from Steinberg's SDK headers, used under its GPLv3 option.
Source: "{#RepoRoot}\external\JUCE\modules\juce_audio_devices\native\asio\LICENSE.txt"; DestDir: "{app}\licenses"; DestName: "LICENSE-ASIO-SDK.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\AmpSim"; Filename: "{app}\AmpSim.exe"; Components: standalone
Name: "{autodesktop}\AmpSim"; Filename: "{app}\AmpSim.exe"; Tasks: desktopicon

; Presets and the unpacked model live in %APPDATA%\AmpSim, which is the user's work and is left
; alone on uninstall, as it is on macOS. Nothing here lists it.

[Messages]
FinishedLabel=Setup has finished installing [name] on your computer.%n%nAmpSim is free software under the GNU Affero General Public License v3. The source code for this version is at {#RepoURL}/tree/v{#AppVersion}.

[Code]
// The build is compiled for AVX2 (CMakeLists.txt). On a processor without it the plugin does not
// fail to load: it faults on the first such instruction and takes the host down with it. So refuse
// here, before anything is installed, and say why.
const
  PF_AVX2_INSTRUCTIONS_AVAILABLE = 40;

function IsProcessorFeaturePresent(ProcessorFeature: Cardinal): Integer;
  external 'IsProcessorFeaturePresent@kernel32.dll stdcall';

function InitializeSetup(): Boolean;
begin
  Result := IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) <> 0;

  if not Result then
    SuppressibleMsgBox('AmpSim needs a processor with AVX2: an Intel Core from 2013 (Haswell) or ' +
                       'later, or any AMD Ryzen. This computer''s processor does not have it, so ' +
                       'AmpSim would crash rather than run, and has not been installed.',
                       mbCriticalError, MB_OK, IDOK);
end;

// The system VST3 folder for everyone, the per-user one otherwise. Both are where the VST3
// specification says a host looks; an install for one user needs no administrator rights.
function Vst3Dir(Param: String): String;
begin
  if IsAdminInstallMode then
    Result := ExpandConstant('{commoncf64}\VST3')
  else
    Result := ExpandConstant('{localappdata}\Programs\Common\VST3');
end;

; ZT1 Lab's installer (Inno Setup 6). Built by
; engine-build-resources/pack_lab.py, which passes:
;   /DVersion=2026.10.19   the release's version
;   /DSource=<repo>        the repo's folder (the licence, the app icon)
;   /DApp=<lab-app/.lab-pack>   electron-builder's win-unpacked/ and tree/
;   /DTree=<...>/tree      what the Lab carries (engine, re, clangd, ghidra)
;
; It installs for the player alone, with no admin prompt, into
; %LocalAppData%\Programs\ZT1-Lab:
;   ZT1 Lab.exe          the app (Electron of its own, the page inside
;                        resources/app.asar) - the same app as in the repo's
;                        lab-app/, run by ZT1 Lab.cmd
;   tree\engine\         zt1-engine.exe, built for release, plus the hook for
;                        the original game (Play views)
;   tree\re\             the knowledge base and the Lab's Python tools
;   tree\clangd\         the C++ language server
;   tree\python\         a Python of its own (capstone, pefile, unicorn), so the
;                        Scene view and the traces work with none installed
;   tree\ghidra\         Ghidra, and tree\java\ the JDK it runs on, so
;                        decompiling works with no Java installed. No project of
;                        the game ships: one is built out of zoo.exe, which is
;                        the game and not ours to hand out
;
; None of the original game's files are here: the Lab reads the player's own
; Zoo Tycoon, and takes the engine's own settings, mods and saves from
; %LocalAppData%\zt1-engine as the engine does.
;
; What it says here about a checkout: the Lab's own tree is read-only (an
; upgrade replaces it), so Write Code, Build engine, Game Files and Play
; original all need the engine's checkout. When there is one, its folder is
; chosen on the page below and written to the Lab's State file
; (%LocalAppData%\zt1-engine\lab-roots.json), which the app reads at start.
; Upgrading (installing over an older version) keeps that choice: the State
; file's directory is read back when it's there.

#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Source
  #define Source ".."
#endif
#ifndef App
  #define App Source + "..\lab-app\.lab-pack"
#endif
#ifndef Tree
  #define Tree App + "\tree"
#endif

[Setup]
AppId={{7C51A9E4-9F2B-4C6E-9E17-2B6A5D4F8C31}
AppName=ZT1 Lab
AppVersion={#Version}
AppVerName=ZT1 Lab {#Version}
AppPublisher=batty251 and the ZT1-Engine contributors
AppPublisherURL=https://github.com/AlexC1991/zt1-engine
AppSupportURL=https://github.com/AlexC1991/zt1-engine/issues
AppUpdatesURL=https://github.com/AlexC1991/zt1-engine/releases
DefaultDirName={localappdata}\Programs\ZT1-Lab
DefaultGroupName=ZT1 Lab
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#Source}\dist
OutputBaseFilename=ZT1-Lab-Setup-{#Version}
SetupIconFile={#Source}\lab-app\icon.ico
UninstallDisplayIcon={app}\ZT1 Lab.exe
LicenseFile={#Source}\LICENSE.txt
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
; (the Lab's own folder is the engine's neighbour, so neither step is needed)
UsePreviousAppDir=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; (the app: electron-builder's output - exe, DLLs, locales, resources)
Source: "{#App}\win-unpacked\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; (what it carries: the engine, our files, clangd, Ghidra, Java, Python)
Source: "{#Tree}\*"; DestDir: "{app}\tree"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Source}\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Source}\THIRD_PARTY_LICENSES.md"; DestDir: "{app}"; Flags: ignoreversion
; (what the Lab carries that it did not write - clangd, Ghidra, the JDK,
;  Python and its packages. The build writes this from each project's own
;  licence text, and the licence page shows it beside LICENSE.txt)
Source: "{#Tree}\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
; (the written offer of corresponding source for the GPL-covered pieces: the
;  JDK and unicorn. GPLv2 gives a choice - ship the source, or offer it in
;  writing - and this is the offer, naming the exact versions carried)
Source: "{#Tree}\SOURCE-OFFER.txt"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#Source}\lab-app\README.md"; DestDir: "{app}"; DestName: "README.md"; Flags: ignoreversion
Source: "{#Source}\dist\ZT1-Engine-Setup-{#Version}.exe"; DestDir: "{app}"; DestName: "ZT1-Engine-Setup.exe"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\ZT1 Lab"; Filename: "{app}\ZT1 Lab.exe"; WorkingDir: "{app}"
Name: "{group}\ZT1-Engine (the game)"; Filename: "{app}\tree\engine\zt1-engine.exe"; WorkingDir: "{app}\tree\engine"
Name: "{group}\Uninstall ZT1 Lab"; Filename: "{uninstallexe}"
Name: "{autodesktop}\ZT1 Lab"; Filename: "{app}\ZT1 Lab.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\ZT1 Lab.exe"; WorkingDir: "{app}"; Description: "Open the ZT1 Lab"; Flags: nowait postinstall skipifsilent
Filename: "{app}\ZT1 Lab.exe"; WorkingDir: "{app}"; Parameters: "--updated"; Flags: runhidden nowait; Check: IsLabUpdateRelaunch

[Code]
function IsLabUpdateRelaunch: Boolean;
begin
  Result := ExpandConstant('{param:RELAUNCHLAB|0}') = '1';
end;

// A folder is an engine checkout when it has the source and the Lab's files
function LooksLikeCheckout(dir: string): Boolean;
begin
  Result := FileExists(dir + '\CMakeLists.txt') and DirExists(dir + '\src') and DirExists(dir + '\re');
end;

// ---------------------------------------------------------- the checkout
// The engine's own folder, when this PC has one (the Lab's, or an install of
// the engine alone), else Documents\GitHub where the README says to clone it
function DefaultCheckout: string;
var
  p: string;
begin
  // (what it was pointed at before, the engine's own folder, then the usual
  // places a checkout is cloned to. Nothing is offered that isn't a checkout:
  // the Lab reads this folder, so a wrong one would be worse than none)
  p := ExpandConstant('{localappdata}\Programs\ZT1-Engine');
  if LooksLikeCheckout(p) then
  begin
    Result := p;
    exit;
  end;
  p := ExpandConstant('{userdocs}\GitHub\zt1-engine');
  if LooksLikeCheckout(p) then
  begin
    Result := p;
    exit;
  end;
  Result := '';
end;


// Whether the Lab has been here before (its own State file), and what
// checkout it was pointed at
function StateFile: string;
begin
  Result := ExpandConstant('{localappdata}\zt1-engine\lab-roots.json');
end;

// A very small read of the State file's `repo=` line - the format this
// installer writes (key=value, which Inno Setup can do without a JSON lib).
// A {"repo": "..."} file is read too, for a Lab that wrote one before.
function SavedCheckout: string;
var
  s, line: AnsiString;
  lines: TArrayOfString;
  i, eq, p, q: Integer;
begin
  Result := '';
  if not FileExists(StateFile) then
    exit;
  if LoadStringsFromFile(StateFile, lines) then
  begin
    for i := 0 to GetArrayLength(lines) - 1 do
    begin
      line := lines[i];
      eq := Pos('=', line);
      if (eq > 1) and (Trim(Copy(line, 1, eq - 1)) = 'repo') then
      begin
        Result := Trim(Copy(line, eq + 1, Length(line)));
        exit;
      end;
    end;
  end;
  // (an older build wrote JSON: {"repo": "..."}. This finds the value
  //  between the quotes after "repo" - a small JSON parser here would be a
  //  mistake, but the arithmetic has to be right: the old version left the
  //  opening quote on the front and then cut at the quote before it, so an
  //  upgrading player's saved checkout came back empty)
  if LoadStringFromFile(StateFile, s) and (Pos('"repo"', s) > 0) then
  begin
    s := Copy(s, Pos('"repo"', s) + 6, Length(s));   // just past "repo"
    p := Pos(':', s);                                // its colon
    if p > 0 then
    begin
      s := Copy(s, p + 1, Length(s));                // just past the colon
      p := Pos('"', s);                              // the opening quote
      if p > 0 then
      begin
        s := Copy(s, p + 1, Length(s));              // just past it
        q := Pos('"', s);                            // the closing quote
        if q > 1 then
          // (JSON doubles a backslash: un-double it, so the path restored is
          //  exactly the one that was saved)
          Result := Copy(s, 1, q - 1);
          StringChangeEx(Result, '\\', '\', True);
      end;
    end;
  end;
end;

var
  CheckoutPage: TInputDirWizardPage;
  StandaloneCheck: TNewCheckBox;

procedure InitializeWizard;
var
  saved: string;
  useStandalone: Boolean;
begin
  // (upgrading: what it was pointed at before is offered again)
  saved := SavedCheckout;
  CheckoutPage := CreateInputDirPage(wpSelectDir, 'The engine', 'Where is the ZT1-Engine checkout?',
    'Point the Lab at the engine''s source to edit and build it, to open the game''s own files, ' +
    'and to play the original with the Lab''s hook. The bundled engine and Explore work without a checkout; ' +
    'check the option below to install the Lab on its own.',
    False, '');
  // (a page starts with no fields: this is the one it has)
  CheckoutPage.Add('The engine''s source checkout (only if connecting to one):');
  if (saved <> '') and LooksLikeCheckout(saved) then
  begin
    CheckoutPage.Values[0] := saved;
    useStandalone := False;
  end
  else
  begin
    saved := DefaultCheckout;
    CheckoutPage.Values[0] := saved;
    useStandalone := saved = '';
    // TInputDirWizardPage validates that its directory field names an
    // existing folder, even when the field is described as optional. Give it
    // a harmless existing folder and an explicit standalone choice instead.
    if CheckoutPage.Values[0] = '' then
      CheckoutPage.Values[0] := ExpandConstant('{tmp}');
  end;
  StandaloneCheck := TNewCheckBox.Create(CheckoutPage.Surface);
  StandaloneCheck.Parent := CheckoutPage.Surface;
  StandaloneCheck.Caption := 'Use the Lab on its own; do not connect a source checkout';
  StandaloneCheck.Checked := useStandalone;
  StandaloneCheck.Top := CheckoutPage.Surface.Height - StandaloneCheck.Height - ScaleY(8);
  StandaloneCheck.Left := 0;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  // (nobody is at the wizard: an unattended install takes the folder it was
  // given, and reads one that isn't a checkout as no checkout)
  if WizardSilent then
    exit;
  if (CurPageID = CheckoutPage.ID) and not StandaloneCheck.Checked then
  begin
    if (CheckoutPage.Values[0] <> '') and not DirExists(CheckoutPage.Values[0]) then
    begin
      MsgBox('There''s no folder at ' + CheckoutPage.Values[0] + '. Leave it empty to install the Lab on its own.',
        mbError, MB_OK);
      Result := False;
    end
    else if (CheckoutPage.Values[0] <> '') and not LooksLikeCheckout(CheckoutPage.Values[0]) then
    begin
      if MsgBox('That folder doesn''t look like the engine''s checkout (it wants CMakeLists.txt, src\ and re\). ' +
        'Use it anyway?', mbConfirmation, MB_YESNO) = IDNO then
        Result := False;
    end;
  end;
end;

// The Lab's State file: the app itself writes it at every start (runtime.js),
// so this is what an upgrade reads and what the uninstaller says
procedure CurStepChanged(CurStep: TSetupStep);
var
  d, body: string;
begin
  if CurStep <> ssPostInstall then
    exit;
  d := ExpandConstant('{localappdata}\zt1-engine');
  ForceDirectories(d);
  body := 'app=' + ExpandConstant('{app}') + #13#10;
  // (only a folder that is the engine's checkout: the Lab reads it, and a
  // wrong one would leave it worse off than no checkout at all)
  if not StandaloneCheck.Checked and LooksLikeCheckout(CheckoutPage.Values[0]) then
    body := body + 'repo=' + CheckoutPage.Values[0] + #13#10
  else
    body := body + 'repo=' + #13#10;
  SaveStringToFile(StateFile, body, False);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  // (what the Lab and the engine keep for the player - the knowledge the Lab
  // learned, its recordings, the engine's settings and mods - is left alone,
  // as the engine's installer does; the uninstall page says where it is)
  if CurUninstallStep = usPostUninstall then
    MsgBox('ZT1 Lab is gone. What it kept for you is still there:' + #13#10#13#10 +
      ExpandConstant('{localappdata}') + '\zt1-engine' + #13#10#13#10 +
      'Delete that folder to remove your recordings and settings as well.', mbInformation, MB_OK);
end;

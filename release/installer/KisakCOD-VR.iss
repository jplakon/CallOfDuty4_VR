#preproc ispp

#ifndef MyVersion
  #error MyVersion must be supplied by tools/build_installer.py
#endif
#ifndef MyPayloadDir
  #error MyPayloadDir must be supplied by tools/build_installer.py
#endif
#ifndef MyManifest
  #error MyManifest must be supplied by tools/build_installer.py
#endif
#ifndef MyManifestSha256
  #error MyManifestSha256 must be supplied by tools/build_installer.py
#endif
#ifndef MyPayloadCount
  #error MyPayloadCount must be supplied by tools/build_installer.py
#endif
#ifndef MyAppId
  #define MyAppId "{{8A7413D7-9D08-4C56-8E75-9C2E6F4D1701}"
#endif

; KISAKCOD_VR_GUARDED_CLASSIC_INSTALLER_V97
; The payload manifest is generated from the same allowlist used by the
; portable ZIP. The [Code] section preserves every destination file that
; existed before Setup first managed that path and restores it on uninstall.

[Setup]
AppId={#MyAppId}
AppName=KisakCOD VR
AppVersion={#MyVersion}
AppVerName=KisakCOD VR v{#MyVersion}
AppPublisher=KisakCOD VR
AppPublisherURL=https://github.com/jplakon/CallOfDuty4_VR
AppSupportURL=https://github.com/jplakon/CallOfDuty4_VR/issues
AppUpdatesURL=https://github.com/jplakon/CallOfDuty4_VR/releases
DefaultDirName={code:GetDefaultInstallDir}
DefaultGroupName=KisakCOD VR
DisableProgramGroupPage=yes
AllowNoIcons=yes
DirExistsWarning=no
DisableWelcomePage=no
LicenseFile={#MyPayloadDir}\LICENSE-GPLv3.txt
InfoBeforeFile={#MyPayloadDir}\README-FIRST.txt
OutputDir=.
OutputBaseFilename=KisakCOD-VR-v{#MyVersion}-Setup
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
UsePreviousAppDir=yes
MinVersion=10.0
WizardStyle=modern
CloseApplications=yes
CloseApplicationsFilter=*.exe,*.dll
RestartApplications=no
RestartIfNeededByRun=no
SetupLogging=yes
Uninstallable=yes
UninstallDisplayName=KisakCOD VR v{#MyVersion}
UninstallDisplayIcon={app}\KisakCOD-VR-Configurator.exe
SignedUninstaller=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#MyPayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#MyManifest}"; Flags: dontcopy

[Icons]
Name: "{group}\Configure KisakCOD VR"; Filename: "{app}\KisakCOD-VR-Configurator.exe"; WorkingDir: "{app}"
Name: "{group}\Launch KisakCOD VR"; Filename: "{app}\Launch-KisakCOD-VR.bat"; WorkingDir: "{app}"
Name: "{group}\Launch KisakCOD VR Diagnostics"; Filename: "{app}\Launch-KisakCOD-VR-Diagnostics.bat"; WorkingDir: "{app}"
Name: "{group}\Uninstall KisakCOD VR"; Filename: "{uninstallexe}"
Name: "{autodesktop}\KisakCOD VR"; Filename: "{app}\Launch-KisakCOD-VR.bat"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\KisakCOD-VR-Configurator.exe"; WorkingDir: "{app}"; Description: "Open KisakCOD VR Configurator"; Flags: nowait postinstall skipifsilent

[Code]
const
  InstallerDataDirectoryName = 'KisakCOD-VR-Installer';
  OriginalFilesListName = 'original-files.txt';
  ManagedFilesListName = 'managed-files.txt';
  BackupCompleteName = 'backup-complete.txt';
  InstallReceiptName = 'install-receipt.txt';
  PayloadManifestName = 'payload-manifest.txt';

var
  DetectedEdition: String;
  DetectedLanguage: String;

function NormalizedDirectory(Value: String): String;
begin
  StringChangeEx(Value, '/', '\', True);
  while (Length(Value) > 3) and
        ((Value[Length(Value)] = '\') or (Value[Length(Value)] = '/')) do
    Delete(Value, Length(Value), 1);
  Result := Value;
end;

function NormalizedRelativePath(Value: String): String;
begin
  Value := Trim(Value);
  StringChangeEx(Value, '/', '\', True);
  Result := Value;
end;

function IsSafeRelativePath(const Value: String): Boolean;
var
  Padded: String;
begin
  Result := False;
  if (Value = '') or (Value[1] = '\') or
     (Pos(':', Value) <> 0) or (Pos('\\', Value) <> 0) then
    Exit;

  Padded := '\' + Lowercase(Value) + '\';
  if (Pos('\..\', Padded) <> 0) or (Pos('\.\', Padded) <> 0) then
    Exit;

  Result := True;
end;

function ArrayContains(
  const Values: TArrayOfString;
  const Value: String): Boolean;
var
  Index: Integer;
begin
  Result := False;
  for Index := 0 to GetArrayLength(Values) - 1 do
    if CompareText(Values[Index], Value) = 0 then
    begin
      Result := True;
      Exit;
    end;
end;

procedure AddUnique(var Values: TArrayOfString; const Value: String);
var
  NewLength: Integer;
begin
  if ArrayContains(Values, Value) then
    Exit;
  NewLength := GetArrayLength(Values);
  SetArrayLength(Values, NewLength + 1);
  Values[NewLength] := Value;
end;

function ReadPathList(
  const FileName: String;
  var Values: TArrayOfString): Boolean;
var
  Loaded: TArrayOfString;
  Index: Integer;
  Value: String;
begin
  SetArrayLength(Values, 0);
  if not FileExists(FileName) then
  begin
    Result := True;
    Exit;
  end;

  if not LoadStringsFromFile(FileName, Loaded) then
  begin
    Result := False;
    Exit;
  end;

  for Index := 0 to GetArrayLength(Loaded) - 1 do
  begin
    Value := NormalizedRelativePath(Loaded[Index]);
    if Value <> '' then
      AddUnique(Values, Value);
  end;
  Result := True;
end;

function SavePathList(
  const FileName: String;
  const Values: TArrayOfString): Boolean;
var
  TemporaryName: String;
begin
  TemporaryName := FileName + '.tmp';
  DeleteFile(TemporaryName);
  Result := SaveStringsToUTF8File(
    TemporaryName, Values, False);
  if not Result then
    Exit;
  Result := CopyFile(TemporaryName, FileName, False);
  DeleteFile(TemporaryName);
end;

function ReadGameLanguage(
  const Directory: String;
  var Language: String): Boolean;
var
  Lines: TArrayOfString;
  Index: Integer;
  Character: Char;
begin
  Result := False;
  Language := '';
  if not LoadStringsFromFile(
      AddBackslash(Directory) + 'localization.txt', Lines) then
    Exit;
  if GetArrayLength(Lines) = 0 then
    Exit;

  Language := Trim(Lines[0]);
  if Language = '' then
    Exit;
  for Index := 1 to Length(Language) do
  begin
    Character := Language[Index];
    if not (((Character >= 'a') and (Character <= 'z')) or
            ((Character >= 'A') and (Character <= 'Z')) or
            ((Character >= '0') and (Character <= '9')) or
            (Character = '_') or (Character = '-')) then
      Exit;
  end;
  Result := True;
end;

function HasMainIwd(const Directory: String): Boolean;
var
  FindRec: TFindRec;
begin
  Result := False;
  if FindFirst(AddBackslash(Directory) + 'main\*.iwd', FindRec) then
  begin
    try
      repeat
        if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then
        begin
          Result := True;
          Exit;
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;

function IsValidClassicInstall(
  const Directory: String;
  var Language: String;
  var Reason: String): Boolean;
var
  Root: String;
begin
  Result := False;
  Root := NormalizedDirectory(Directory);
  Language := '';

  if not FileExists(AddBackslash(Root) + 'iw3sp.exe') then
  begin
    Reason := 'The selected folder does not contain iw3sp.exe.' + #13#10 + #13#10 +
      'Select the original 2007 Call of Duty 4 folder, not a parent folder ' +
      'or the remastered game. Microsoft/Xbox automatic raw-layout ' +
      'normalization is pending a verified file map; Setup will not guess ' +
      'or move game files.';
    Exit;
  end;

  if not FileExists(AddBackslash(Root) + 'localization.txt') then
  begin
    Reason := 'The selected folder is missing localization.txt. This file is ' +
      'required to identify the installed game language safely.';
    Exit;
  end;

  if not ReadGameLanguage(Root, Language) then
  begin
    Reason := 'The first line of localization.txt is empty or unsafe. Setup ' +
      'cannot validate the matching language fastfiles.';
    Exit;
  end;

  if not DirExists(AddBackslash(Root) + 'main') then
  begin
    Reason := 'The selected folder is missing the main directory.';
    Exit;
  end;

  if not HasMainIwd(Root) then
  begin
    Reason := 'The main directory contains no .iwd game archives. KisakCOD VR ' +
      'does not include or download original COD4 game data.';
    Exit;
  end;

  if not FileExists(
      AddBackslash(Root) + 'zone\' + Language + '\code_post_gfx.ff') then
  begin
    Reason := 'The selected folder is missing zone\' + Language +
      '\code_post_gfx.ff, which must match the language named by ' +
      'localization.txt.';
    Exit;
  end;

  Reason := '';
  Result := True;
end;

function EditionForDirectory(const Directory: String): String;
begin
  if Pos('\steamapps\common\', Lowercase(Directory) + '\') <> 0 then
    Result := 'Steam'
  else
    Result := 'classic-compatible';
end;

function TryGameCandidate(
  const Candidate: String;
  var Directory: String): Boolean;
var
  Language: String;
  Reason: String;
  Normalized: String;
begin
  Result := False;
  Normalized := NormalizedDirectory(Candidate);
  if IsValidClassicInstall(Normalized, Language, Reason) then
  begin
    Directory := Normalized;
    Result := True;
  end;
end;

function VdfPathFromLine(const Line: String): String;
var
  MarkerPosition: Integer;
  QuotePosition: Integer;
  EndQuotePosition: Integer;
  Tail: String;
begin
  Result := '';
  MarkerPosition := Pos('"path"', Lowercase(Line));
  if MarkerPosition = 0 then
    Exit;

  Tail := Copy(Line, MarkerPosition + Length('"path"'), Length(Line));
  QuotePosition := Pos('"', Tail);
  if QuotePosition = 0 then
    Exit;
  Tail := Copy(Tail, QuotePosition + 1, Length(Tail));
  EndQuotePosition := Pos('"', Tail);
  if EndQuotePosition = 0 then
    Exit;

  Result := Copy(Tail, 1, EndQuotePosition - 1);
  StringChangeEx(Result, '\\', '\', True);
  StringChangeEx(Result, '\/', '/', True);
end;

function FindGameInSteamLibraryFile(
  const LibraryFile: String;
  var Directory: String): Boolean;
var
  Lines: TArrayOfString;
  Index: Integer;
  LibraryPath: String;
begin
  Result := False;
  if not LoadStringsFromFile(LibraryFile, Lines) then
    Exit;

  for Index := 0 to GetArrayLength(Lines) - 1 do
  begin
    LibraryPath := VdfPathFromLine(Lines[Index]);
    if (LibraryPath <> '') and TryGameCandidate(
        AddBackslash(LibraryPath) +
          'steamapps\common\Call of Duty 4', Directory) then
    begin
      Result := True;
      Exit;
    end;
  end;
end;

function FindSteamGame(var Directory: String): Boolean;
var
  SteamPath: String;
  InstallLocation: String;
begin
  Result := False;

  if RegQueryStringValue(
      HKLM32,
      'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 7940',
      'InstallLocation', InstallLocation) and
      TryGameCandidate(InstallLocation, Directory) then
  begin
    Result := True;
    Exit;
  end;

  if RegQueryStringValue(
      HKCU,
      'Software\Valve\Steam',
      'SteamPath', SteamPath) then
  begin
    if TryGameCandidate(
        AddBackslash(SteamPath) +
          'steamapps\common\Call of Duty 4', Directory) then
    begin
      Result := True;
      Exit;
    end;

    if FindGameInSteamLibraryFile(
        AddBackslash(SteamPath) + 'steamapps\libraryfolders.vdf',
        Directory) then
    begin
      Result := True;
      Exit;
    end;
  end;

  if TryGameCandidate(
      ExpandConstant('{pf32}\Steam\steamapps\common\Call of Duty 4'),
      Directory) then
    Result := True;
end;

function GetDefaultInstallDir(Param: String): String;
begin
  if FindSteamGame(Result) then
    Exit;
  Result := ExpandConstant(
    '{pf32}\Steam\steamapps\common\Call of Duty 4');
end;

function ValidateSelectedGameDirectory(var Reason: String): Boolean;
begin
  Result := IsValidClassicInstall(
    WizardDirValue, DetectedLanguage, Reason);
  if Result then
    DetectedEdition := EditionForDirectory(WizardDirValue);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Reason: String;
begin
  Result := True;
  if CurPageID <> wpSelectDir then
    Exit;

  if not ValidateSelectedGameDirectory(Reason) then
  begin
    SuppressibleMsgBox(
      Reason + #13#10 + #13#10 + 'No game or mod files were changed.',
      mbError, MB_OK, IDOK);
    Result := False;
  end;
end;

function PreparePayloadBackups: String;
var
  ManifestPath: String;
  Manifest: TArrayOfString;
  Originals: TArrayOfString;
  Managed: TArrayOfString;
  Marker: TArrayOfString;
  DataRoot: String;
  OriginalRoot: String;
  OriginalsPath: String;
  ManagedPath: String;
  RelativePath: String;
  TargetPath: String;
  BackupPath: String;
  TemporaryBackup: String;
  Index: Integer;
begin
  Result := '';
  ManifestPath := ExpandConstant('{tmp}\') + PayloadManifestName;
  try
    ExtractTemporaryFile(PayloadManifestName);
  except
    Result := 'Setup could not extract its guarded payload manifest.';
    Exit;
  end;

  if CompareText(
      GetSHA256OfFile(ManifestPath), '{#MyManifestSha256}') <> 0 then
  begin
    Result := 'The embedded payload manifest failed its SHA-256 check. ' +
      'Download Setup again.';
    Exit;
  end;

  if not ReadPathList(ManifestPath, Manifest) then
  begin
    Result := 'Setup could not read its guarded payload manifest.';
    Exit;
  end;
  if GetArrayLength(Manifest) <> {#MyPayloadCount} then
  begin
    Result := 'The guarded payload manifest has an unexpected file count.';
    Exit;
  end;

  DataRoot := AddBackslash(ExpandConstant('{app}')) +
    InstallerDataDirectoryName;
  OriginalRoot := AddBackslash(DataRoot) + 'original-files';
  OriginalsPath := AddBackslash(DataRoot) + OriginalFilesListName;
  ManagedPath := AddBackslash(DataRoot) + ManagedFilesListName;

  if not ForceDirectories(OriginalRoot) then
  begin
    Result := 'Setup could not create its private backup directory: ' +
      OriginalRoot;
    Exit;
  end;
  if not ReadPathList(OriginalsPath, Originals) then
  begin
    Result := 'Setup could not read the original-file backup index.';
    Exit;
  end;
  if not ReadPathList(ManagedPath, Managed) then
  begin
    Result := 'Setup could not read the managed-file index.';
    Exit;
  end;

  for Index := 0 to GetArrayLength(Manifest) - 1 do
  begin
    RelativePath := NormalizedRelativePath(Manifest[Index]);
    if not IsSafeRelativePath(RelativePath) then
    begin
      Result := 'Setup rejected an unsafe payload path: ' + RelativePath;
      Exit;
    end;

    if not ArrayContains(Managed, RelativePath) then
    begin
      TargetPath := AddBackslash(ExpandConstant('{app}')) + RelativePath;
      BackupPath := AddBackslash(OriginalRoot) + RelativePath;

      if FileExists(BackupPath) then
        AddUnique(Originals, RelativePath)
      else if FileExists(TargetPath) then
      begin
        if not ForceDirectories(ExtractFileDir(BackupPath)) then
        begin
          Result := 'Setup could not create a backup subdirectory for: ' +
            RelativePath;
          Exit;
        end;

        TemporaryBackup := BackupPath + '.tmp';
        DeleteFile(TemporaryBackup);
        if not CopyFile(TargetPath, TemporaryBackup, True) then
        begin
          Result := 'Setup could not back up the existing file: ' +
            RelativePath;
          Exit;
        end;
        if CompareText(
            GetSHA256OfFile(TargetPath),
            GetSHA256OfFile(TemporaryBackup)) <> 0 then
        begin
          DeleteFile(TemporaryBackup);
          Result := 'The backup copy failed verification for: ' +
            RelativePath;
          Exit;
        end;
        if not RenameFile(TemporaryBackup, BackupPath) then
        begin
          DeleteFile(TemporaryBackup);
          Result := 'Setup could not commit the verified backup for: ' +
            RelativePath;
          Exit;
        end;
        AddUnique(Originals, RelativePath);
      end;
      AddUnique(Managed, RelativePath);
    end;
  end;

  if not SavePathList(OriginalsPath, Originals) then
  begin
    Result := 'Setup could not save the original-file backup index.';
    Exit;
  end;
  if not SavePathList(ManagedPath, Managed) then
  begin
    Result := 'Setup could not save the managed-file index.';
    Exit;
  end;

  SetArrayLength(Marker, 4);
  Marker[0] := 'KisakCOD VR guarded backup';
  Marker[1] := 'InstallerVersion={#MyVersion}';
  Marker[2] := 'PayloadManifestSHA256={#MyManifestSha256}';
  Marker[3] := 'OriginalFileCount=' +
    IntToStr(GetArrayLength(Originals));
  if not SavePathList(
      AddBackslash(DataRoot) + BackupCompleteName, Marker) then
    Result := 'Setup could not commit its backup-complete receipt.';
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Reason: String;
begin
  NeedsRestart := False;
  Result := '';
  if not ValidateSelectedGameDirectory(Reason) then
  begin
    Result := Reason + #13#10 + #13#10 +
      'No game or mod files were changed.';
    Exit;
  end;

  Result := PreparePayloadBackups;
end;

function RegisterCloseApplicationsResource(
  const FileName: String): Boolean;
begin
#if VER >= EncodeVer(7, 0, 0)
  Result := RegisterExtraCloseApplicationsResource(FileName);
#else
  Result := RegisterExtraCloseApplicationsResource(False, FileName);
#endif
end;

procedure RegisterExtraCloseApplicationsResources;
begin
  RegisterCloseApplicationsResource(
    AddBackslash(ExpandConstant('{app}')) + 'iw3sp.exe');
  RegisterCloseApplicationsResource(
    AddBackslash(ExpandConstant('{app}')) + 'KisakCOD-sp.exe');
  RegisterCloseApplicationsResource(
    AddBackslash(ExpandConstant('{app}')) +
      'KisakCOD-VR-Configurator.exe');
  RegisterCloseApplicationsResource(
    AddBackslash(ExpandConstant('{app}')) +
      'KisakCOD-VR-Input-Mapper.exe');
end;

procedure WriteInstallReceipt;
var
  Receipt: TArrayOfString;
  DataRoot: String;
begin
  DataRoot := AddBackslash(ExpandConstant('{app}')) +
    InstallerDataDirectoryName;
  if not ForceDirectories(DataRoot) then
    Exit;

  SetArrayLength(Receipt, 8);
  Receipt[0] := 'KisakCOD VR guarded installer receipt';
  Receipt[1] := 'InstallerVersion={#MyVersion}';
  Receipt[2] := 'PayloadManifestSHA256={#MyManifestSha256}';
  Receipt[3] := 'PayloadFileCount={#MyPayloadCount}';
  Receipt[4] := 'DetectedEdition=' + DetectedEdition;
  Receipt[5] := 'DetectedLanguage=' + DetectedLanguage;
  Receipt[6] := 'TargetDirectory=' + ExpandConstant('{app}');
  Receipt[7] := 'InstalledLocalTime=' +
    GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':');
  SavePathList(AddBackslash(DataRoot) + InstallReceiptName, Receipt);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    WriteInstallReceipt;
end;

function RestoreOriginalFiles(var FailureReason: String): Boolean;
var
  DataRoot: String;
  OriginalRoot: String;
  OriginalsPath: String;
  Originals: TArrayOfString;
  RelativePath: String;
  BackupPath: String;
  TargetPath: String;
  TemporaryTarget: String;
  Index: Integer;
begin
  Result := False;
  FailureReason := '';
  DataRoot := AddBackslash(ExpandConstant('{app}')) +
    InstallerDataDirectoryName;
  if not DirExists(DataRoot) then
  begin
    Result := True;
    Exit;
  end;

  OriginalsPath := AddBackslash(DataRoot) + OriginalFilesListName;
  OriginalRoot := AddBackslash(DataRoot) + 'original-files';
  if not FileExists(OriginalsPath) then
  begin
    FailureReason := 'The original-file backup index is missing. Backup ' +
      'data was preserved at: ' + DataRoot;
    Exit;
  end;
  if not ReadPathList(OriginalsPath, Originals) then
  begin
    FailureReason := 'The original-file backup index could not be read. ' +
      'Backup data was preserved at: ' + DataRoot;
    Exit;
  end;

  for Index := 0 to GetArrayLength(Originals) - 1 do
  begin
    RelativePath := NormalizedRelativePath(Originals[Index]);
    if not IsSafeRelativePath(RelativePath) then
    begin
      FailureReason := 'Uninstall rejected an unsafe backup path: ' +
        RelativePath;
      Exit;
    end;

    BackupPath := AddBackslash(OriginalRoot) + RelativePath;
    TargetPath := AddBackslash(ExpandConstant('{app}')) + RelativePath;
    if not FileExists(BackupPath) then
    begin
      FailureReason := 'An original backup file is missing: ' + RelativePath +
        '. Remaining backup data was preserved at: ' + DataRoot;
      Exit;
    end;
    if not ForceDirectories(ExtractFileDir(TargetPath)) then
    begin
      FailureReason := 'Uninstall could not recreate a directory for: ' +
        RelativePath;
      Exit;
    end;

    TemporaryTarget := TargetPath + '.kisakcod-restore.tmp';
    DeleteFile(TemporaryTarget);
    if not CopyFile(BackupPath, TemporaryTarget, True) then
    begin
      FailureReason := 'Uninstall could not stage the original file: ' +
        RelativePath;
      Exit;
    end;
    if CompareText(
        GetSHA256OfFile(BackupPath),
        GetSHA256OfFile(TemporaryTarget)) <> 0 then
    begin
      DeleteFile(TemporaryTarget);
      FailureReason := 'The restore copy failed verification for: ' +
        RelativePath;
      Exit;
    end;
    DeleteFile(TargetPath);
    if not RenameFile(TemporaryTarget, TargetPath) then
    begin
      DeleteFile(TemporaryTarget);
      FailureReason := 'Uninstall could not restore the original file: ' +
        RelativePath;
      Exit;
    end;
  end;

  if not DelTree(DataRoot, True, True, True) then
  begin
    FailureReason := 'Original files were restored, but installer metadata ' +
      'could not be removed from: ' + DataRoot;
    Exit;
  end;
  Result := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  FailureReason: String;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;

  if not RestoreOriginalFiles(FailureReason) then
  begin
    Log('KisakCOD VR original-file restoration failed: ' + FailureReason);
    if not UninstallSilent then
      MsgBox(
        'KisakCOD VR was removed, but Setup could not finish restoring every ' +
        'pre-install file.' + #13#10 + #13#10 + FailureReason,
        mbError, MB_OK);
  end;
end;

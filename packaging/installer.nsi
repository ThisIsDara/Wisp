; ============================================================
; wisp installer - per-user NSIS (D-13), no admin/UAC
; Pattern: nsis.sourceforge.io/Examples/install-per-user.nsi
; Payload: build/deploy/wisp (windeployqt output, produced by
;          deploy.ps1 via build-installer.ps1)
; ============================================================
!include "MUI2.nsh"
!include "LogicLib.nsh"

Name "wisp"
OutFile "..\build\deploy\wisp-setup.exe"
RequestExecutionLevel user          ; CRITICAL: no UAC, per-user install (Pitfall 2)
InstallDir "$LocalAppData\Programs\wisp"
Caption "wisp — app launcher"

; --- version metadata (SignPath artifact policy: product name/version) --
; BUMP IN LOCKSTEP with CMakeLists project(VERSION ...) every release -
; the updater compares WISP_VERSION against the release tag (Phase 8 D-15).
VIProductVersion "0.1.3.0"
VIAddVersionKey "ProductName" "wisp"
VIAddVersionKey "FileDescription" "wisp — app launcher"
VIAddVersionKey "FileVersion" "0.1.3"
VIAddVersionKey "ProductVersion" "0.1.3"
VIAddVersionKey "CompanyName" "ThisIsDara"
VIAddVersionKey "LegalCopyright" "© 2026 ThisIsDara"

; --- UI copy (locked by UI-SPEC) ----------------------------------
!define MUI_WELCOMEPAGE_TEXT "This will install wisp — the app launcher for Windows.$\r$\n$\r$\nClick Next to continue."
!define MUI_FINISHPAGE_TEXT "wisp has been installed. Use the wisp tray icon to open the launcher anytime."
!define MUI_FINISHPAGE_RUN "$InstDir\wisp.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Run wisp now"
!define MUI_FINISHPAGE_RUN_PARAMETERS "--autostart"   ; resident-hidden: launches into the tray
!define MUI_ABORTWARNING

; --- pages --------------------------------------------------------
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS   ; 2026-08-15: "Start with Windows" checkbox
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; --- per-user shell folders (install AND uninstall) ---------------
Function .onInit
  SetShellVarContext Current
FunctionEnd

Function un.onInit
  SetShellVarContext Current
FunctionEnd

; --- install ------------------------------------------------------
Section "wisp" SecWisp
  SectionIn RO   ; core payload — always installed
  ; wisp is tray-resident — manual double-click while running locks Qt DLLs
  ; (Qt6QmlModels.dll etc.). Close it first so File can overwrite.
  ExecWait '"$WINDIR\System32\taskkill.exe" /f /im wisp.exe' ; ignore exit code if not running
  Sleep 400
  SetOutPath "$InstDir"
  File /r "..\build\deploy\wisp\*"   ; wisp.exe, Qt DLLs, plugins, qml, qt.conf, NOTICES, vc_redist.x64.exe
  WriteUninstaller "$InstDir\Uninst.exe"

  WriteRegStr   HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\wisp" "DisplayName" "wisp"
  WriteRegStr   HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\wisp" "UninstallString" '"$InstDir\Uninst.exe"'
  WriteRegStr   HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\wisp" "DisplayIcon" "$InstDir\wisp.exe"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\wisp" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\wisp" "NoRepair" 1

  ; VC_redist gate (D-14): execute the carried redist ONLY when the
  ; VC++ 2015-2022 x64 runtime is not already installed.
  ReadRegDWORD $0 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" "Installed"
  ${If} $0 != 1
    ExecWait '"$InstDir\vc_redist.x64.exe" /install /quiet /norestart'
  ${EndIf}

  ; --- Phase 8: silent updates must bring wisp back (D-12) ----------
  ; MUI_FINISHPAGE_RUN does not exist in /S mode (research section 3).
  ; The updater quit-waits on us; relaunch resident-hidden exactly as
  ; the boot path does. Interactive installs still use the Finish page.
  ${If} ${Silent}
    Exec '"$InstDir\wisp.exe" --autostart'
  ${EndIf}
  ; NOTE: the downloaded installer copy in %TEMP% is intentionally KEPT
  ; (CONTEXT D-13) so it can be re-run manually if an update misbehaves.
SectionEnd

; --- Start with Windows (2026-08-15) - optional, default ON --------
; Writes the SAME HKCU Run value the in-app toggle uses (D-12 format:
; "\"<exe>\" --autostart"). Unchecking during install leaves/removes it
; per the user's choice - never silently forces autostart on anyone.
;
; Phase 8 silent-update contract (D-06): /S selects ALL sections, which
; would re-enable autostart for users who unchecked it at install. The
; guard lives HERE (not .onInit - ${SecAutostart} is not defined until
; the Section declaration compiles): if OUR Run value is absent, skip
; the write entirely so an opt-out user stays opted out.
Section "Start with Windows" SecAutostart
  ${If} ${Silent}
    ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "wisp"
    ${If} $0 == ""
      Goto done_autostart
    ${EndIf}
  ${EndIf}
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "wisp" '"$InstDir\wisp.exe" --autostart'
  done_autostart:
SectionEnd

; --- Start Menu shortcut ------------------------------------------
Section "Start Menu shortcut"
  CreateShortcut /NoWorkingDir "$SMPrograms\wisp.lnk" "$InstDir\wisp.exe"
SectionEnd

; --- uninstall ----------------------------------------------------
Section "Uninstall"
  Delete "$SMPrograms\wisp.lnk"
  ; 2026-08-15: remove the autostart Run value (matches the in-app toggle;
  ; only the value WE wrote is deleted — guard by the installed path).
  ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "wisp"
  ${If} $0 == '"$InstDir\wisp.exe" --autostart'
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "wisp"
  ${EndIf}
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\wisp"
  RMDir /r "$InstDir"
SectionEnd

; --- section descriptions (component page) ------------------------
; NOTE: placed AFTER the section declarations — ${SecAutostart} must exist
; when this macro expands (makensis warning otherwise).
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecAutostart} "Launch wisp automatically in the tray when you sign in to Windows."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

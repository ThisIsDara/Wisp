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
VIProductVersion "0.1.0.0"
VIAddVersionKey "ProductName" "wisp"
VIAddVersionKey "FileDescription" "wisp — app launcher"
VIAddVersionKey "FileVersion" "0.1.0"
VIAddVersionKey "ProductVersion" "0.1.0"
VIAddVersionKey "CompanyName" "ThisIsDara"
VIAddVersionKey "LegalCopyright" "© 2026 ThisIsDara"

; --- UI copy (locked by UI-SPEC) ----------------------------------
!define MUI_WELCOMEPAGE_TEXT "This will install wisp — the app launcher for Windows.$\r$\n$\r$\nClick Next to continue."
!define MUI_FINISHPAGE_TEXT "wisp has been installed. Use the wisp tray icon to open the launcher anytime."
!define MUI_ABORTWARNING

; --- pages --------------------------------------------------------
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
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
Section "Install"
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
SectionEnd

; --- Start Menu shortcut ------------------------------------------
Section "Start Menu shortcut"
  CreateShortcut /NoWorkingDir "$SMPrograms\wisp.lnk" "$InstDir\wisp.exe"
SectionEnd

; --- uninstall ----------------------------------------------------
Section "Uninstall"
  Delete "$SMPrograms\wisp.lnk"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\wisp"
  RMDir /r "$InstDir"
SectionEnd

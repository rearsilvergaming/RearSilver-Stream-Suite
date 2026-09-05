Unicode True
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

!ifndef RS_ARTIFACT_ROOT
  !error "RS_ARTIFACT_ROOT must point to a clean artifacts/<profile> directory."
!endif
!ifndef RS_PREREQUISITE_ROOT
  !error "RS_PREREQUISITE_ROOT must contain the verified Microsoft prerequisite installers."
!endif
!ifndef RS_VERSION
  !define RS_VERSION "1.0.0"
!endif
!ifndef RS_CHANNEL
  !define RS_CHANNEL "Release"
!endif
!ifndef RS_OUTPUT_FILE
  !define RS_OUTPUT_FILE "RearSilver-Stream-Suite-Setup.exe"
!endif

!define PRODUCT_NAME "RearSilver Stream Suite"
!define PRODUCT_PUBLISHER "RearSilver Gaming"
!define PRODUCT_WEB_SITE "https://github.com/rearsilvergaming/RearSilver-Stream-Suite"
!define PRODUCT_REG_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\RearSilver Stream Suite"

Name "${PRODUCT_NAME} | ${RS_CHANNEL}"
OutFile "${RS_OUTPUT_FILE}"
InstallDir "$PROGRAMFILES64\RearSilver Stream Suite"
InstallDirRegKey HKLM "${PRODUCT_REG_KEY}" "InstallLocation"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
SetCompressorDictSize 64
ManifestDPIAware true

!define MUI_ICON "assets\branding\rearsilver-stream-suite.ico"
!define MUI_UNICON "assets\branding\rearsilver-stream-suite.ico"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "assets\branding\installer-header.bmp"
!define MUI_WELCOMEFINISHPAGE_BITMAP "assets\branding\installer-welcome.bmp"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_NOAUTOCLOSE
!define MUI_HEADER_TEXT "RearSilver Stream Suite"
!define MUI_HEADER_SUBTEXT "OBS tools and the Control Hub"
!define MUI_WELCOMEPAGE_TITLE "Welcome to RearSilver Stream Suite"
!define MUI_WELCOMEPAGE_TITLE_3LINES
!define MUI_WELCOMEPAGE_TEXT "This setup installs RearSilver Stream Suite ${RS_VERSION} (${RS_CHANNEL}).$\r$\n$\r$\nThe OBS plugin and Control Hub are installed as one managed product and can be updated or removed cleanly. Close OBS Studio and the Control Hub before continuing."
!define MUI_FINISHPAGE_TITLE "RearSilver Stream Suite is ready"
!define MUI_FINISHPAGE_TITLE_3LINES
!define MUI_FINISHPAGE_TEXT "RearSilver Stream Suite has been installed successfully.$\r$\n$\r$\nStart OBS Studio to open the Suite dock and Control Hub."
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Launch OBS Studio"
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchOBS

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "License.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "English"

Var ObsDir

Function FindObsDirectory
  StrCpy $ObsDir "$PROGRAMFILES64\obs-studio"
  ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio" "InstallLocation"
  ${If} $0 != ""
    StrCpy $ObsDir $0
  ${EndIf}
FunctionEnd

Function .onInit
  SetRegView 64
  Call FindObsDirectory
  IfFileExists "$ObsDir\bin\64bit\obs64.exe" obs_found
    MessageBox MB_ICONSTOP|MB_OK "OBS Studio could not be found at $ObsDir. Install the 64-bit version of OBS Studio before installing RearSilver Stream Suite."
    Abort
  obs_found:
FunctionEnd

Function HasWebView2Runtime
  Push $0
  Push $1
  StrCpy $1 "0"
  SetRegView 32
  ReadRegStr $0 HKLM "Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}" "pv"
  ${If} $0 != ""
  ${AndIf} $0 != "0.0.0.0"
    StrCpy $1 "1"
  ${Else}
    ReadRegStr $0 HKCU "Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}" "pv"
    ${If} $0 != ""
    ${AndIf} $0 != "0.0.0.0"
      StrCpy $1 "1"
    ${EndIf}
  ${EndIf}
  SetRegView 64
  Pop $0
  Exch $1
FunctionEnd

Function InstallPrerequisites
  SetOutPath "$PLUGINSDIR"

  DetailPrint "Checking Microsoft Visual C++ Runtime..."
  File "/oname=$PLUGINSDIR\vc_redist.x64.exe" "${RS_PREREQUISITE_ROOT}\vc_redist.x64.exe"
  ExecWait '"$PLUGINSDIR\vc_redist.x64.exe" /install /quiet /norestart' $0
  ${If} $0 != 0
  ${AndIf} $0 != 1638
  ${AndIf} $0 != 3010
    MessageBox MB_ICONSTOP|MB_OK "Microsoft Visual C++ Runtime setup failed with code $0. RearSilver Stream Suite was not installed."
    Abort
  ${EndIf}

  Call HasWebView2Runtime
  Pop $1
  ${If} $1 != "1"
    DetailPrint "Installing Microsoft Edge WebView2 Runtime..."
    File "/oname=$PLUGINSDIR\MicrosoftEdgeWebView2RuntimeInstallerX64.exe" "${RS_PREREQUISITE_ROOT}\MicrosoftEdgeWebView2RuntimeInstallerX64.exe"
    ExecWait '"$PLUGINSDIR\MicrosoftEdgeWebView2RuntimeInstallerX64.exe" /silent /install' $0
    ${If} $0 != 0
    ${AndIf} $0 != 3010
      MessageBox MB_ICONSTOP|MB_OK "Microsoft Edge WebView2 Runtime setup failed with code $0. RearSilver Stream Suite was not installed."
      Abort
    ${EndIf}
    Call HasWebView2Runtime
    Pop $1
    ${If} $1 != "1"
      MessageBox MB_ICONSTOP|MB_OK "Microsoft Edge WebView2 Runtime could not be verified after installation. RearSilver Stream Suite was not installed."
      Abort
    ${EndIf}
  ${EndIf}
FunctionEnd

Section "RearSilver Stream Suite" MainSection
  SetShellVarContext all

  Call InstallPrerequisites

  ; Build inputs are checked by build-installer.ps1 and the File instructions
  ; at compile time. Installation uses embedded files, never developer paths.

  CreateDirectory "$ObsDir\obs-plugins\64bit"
  SetOutPath "$ObsDir\obs-plugins\64bit"
  File "${RS_ARTIFACT_ROOT}\obs-plugins\64bit\RearSilver-Stream-Suite.dll"

  ; Legacy manual deployments left these private directories behind. Never
  ; recurse: non-empty directories must be preserved for separate review.
  RMDir "$ObsDir\obs-plugins\64bit\RearSilver-Stream-Suite\qt-plugins\tls"
  RMDir "$ObsDir\obs-plugins\64bit\RearSilver-Stream-Suite\qt-plugins"
  RMDir "$ObsDir\obs-plugins\64bit\RearSilver-Stream-Suite"
  ClearErrors

  CreateDirectory "$ObsDir\data\obs-plugins\RearSilver-Stream-Suite\locale"
  SetOutPath "$ObsDir\data\obs-plugins\RearSilver-Stream-Suite\locale"
  File "${RS_ARTIFACT_ROOT}\data\obs-plugins\RearSilver-Stream-Suite\locale\en-GB.ini"

  RMDir /r "$INSTDIR\Control Hub"
  SetOutPath "$INSTDIR\Control Hub"
  File /r "${RS_ARTIFACT_ROOT}\control-hub\*.*"

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\RearSilver Stream Suite"
  CreateShortcut "$SMPROGRAMS\RearSilver Stream Suite\RearSilver Stream Suite - Control Hub.lnk" "$INSTDIR\Control Hub\RearSilver-Stream-Suite-Control-Hub.exe"
  CreateShortcut "$SMPROGRAMS\RearSilver Stream Suite\Uninstall RearSilver Stream Suite.lnk" "$INSTDIR\Uninstall.exe"

  WriteRegStr HKLM "${PRODUCT_REG_KEY}" "DisplayName" "${PRODUCT_NAME} (${RS_CHANNEL})"
  WriteRegStr HKLM "${PRODUCT_REG_KEY}" "DisplayVersion" "${RS_VERSION}"
  WriteRegStr HKLM "${PRODUCT_REG_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
  WriteRegStr HKLM "${PRODUCT_REG_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
  WriteRegStr HKLM "${PRODUCT_REG_KEY}" "DisplayIcon" "$INSTDIR\Control Hub\RearSilver-Stream-Suite-Control-Hub.exe"
  WriteRegStr HKLM "${PRODUCT_REG_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${PRODUCT_REG_KEY}" "OBSInstallLocation" "$ObsDir"
  WriteRegStr HKLM "${PRODUCT_REG_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "${PRODUCT_REG_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKLM "${PRODUCT_REG_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${PRODUCT_REG_KEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetShellVarContext all
  SetRegView 64
  ReadRegStr $ObsDir HKLM "${PRODUCT_REG_KEY}" "OBSInstallLocation"

  ${If} $ObsDir != ""
    Delete "$ObsDir\obs-plugins\64bit\RearSilver-Stream-Suite.dll"
    RMDir "$ObsDir\obs-plugins\64bit\RearSilver-Stream-Suite\qt-plugins\tls"
    RMDir "$ObsDir\obs-plugins\64bit\RearSilver-Stream-Suite\qt-plugins"
    RMDir "$ObsDir\obs-plugins\64bit\RearSilver-Stream-Suite"
    ClearErrors
    Delete "$ObsDir\data\obs-plugins\RearSilver-Stream-Suite\locale\en-GB.ini"
    RMDir "$ObsDir\data\obs-plugins\RearSilver-Stream-Suite\locale"
    RMDir "$ObsDir\data\obs-plugins\RearSilver-Stream-Suite"
  ${EndIf}

  RMDir /r "$INSTDIR\Control Hub"
  Delete "$SMPROGRAMS\RearSilver Stream Suite\RearSilver Stream Suite - Control Hub.lnk"
  Delete "$SMPROGRAMS\RearSilver Stream Suite\Uninstall RearSilver Stream Suite.lnk"
  RMDir "$SMPROGRAMS\RearSilver Stream Suite"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "${PRODUCT_REG_KEY}"
SectionEnd

Function LaunchOBS
  ; OBS resolves its data/locale paths relative to its binary directory.
  ; Do not inherit the Control Hub extraction directory from installation.
  SetOutPath "$ObsDir\bin\64bit"
  ClearErrors
  Exec '"$ObsDir\bin\64bit\obs64.exe"'
  IfErrors 0 +2
    MessageBox MB_ICONEXCLAMATION|MB_OK "Setup could not launch OBS Studio. You can open it from your normal shortcut."
FunctionEnd

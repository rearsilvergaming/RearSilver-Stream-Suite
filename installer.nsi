Unicode True
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "nsDialogs.nsh"
!include "WordFunc.nsh"

!ifndef RS_VC_RUNTIME_MIN_VERSION
  !define RS_VC_RUNTIME_MIN_VERSION "0"
!endif

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
SetCompressor lzma
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
!define MUI_DIRECTORYPAGE_TEXT_TOP "Choose where to install the Control Hub and RearSilver Stream Suite files. The OBS plugin location is selected separately on the next page."
!define MUI_DIRECTORYPAGE_TEXT_DESTINATION "Suite destination folder"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "License.txt"
!insertmacro MUI_PAGE_DIRECTORY
Page custom ObsPageCreate ObsPageLeave
Page custom InstallSummaryPageCreate
!insertmacro MUI_PAGE_INSTFILES
!define MUI_PAGE_CUSTOMFUNCTION_PRE FinishPagePre
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "English"

Var ObsDir
Var ObsPathField
Var ObsStatusLabel
Var InstallInstance
Var UpdateHandoff

Function FindObsDirectory
  ; An existing Suite installation is authoritative. This keeps updates on
  ; the same OBS installation even when OBS is installed on another drive.
  StrCpy $ObsDir ""
  SetRegView 64
  ReadRegStr $0 HKLM "${PRODUCT_REG_KEY}" "OBSInstallLocation"
  ${If} $0 != ""
    IfFileExists "$0\bin\64bit\obs64.exe" 0 +2
      StrCpy $ObsDir $0
  ${EndIf}

  ${If} $ObsDir == ""
    ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio" "InstallLocation"
    ${If} $0 != ""
      IfFileExists "$0\bin\64bit\obs64.exe" 0 +2
        StrCpy $ObsDir $0
    ${EndIf}
  ${EndIf}

  ${If} $ObsDir == ""
    ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio" "InstallLocation"
    ${If} $0 != ""
      IfFileExists "$0\bin\64bit\obs64.exe" 0 +2
        StrCpy $ObsDir $0
    ${EndIf}
  ${EndIf}

  ; Some OBS installers register their uninstall entry in the 32-bit view.
  ${If} $ObsDir == ""
    SetRegView 32
    ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio" "InstallLocation"
    ${If} $0 != ""
      IfFileExists "$0\bin\64bit\obs64.exe" 0 +2
        StrCpy $ObsDir $0
    ${EndIf}
  ${EndIf}

  ${If} $ObsDir == ""
    ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio" "InstallLocation"
    ${If} $0 != ""
      IfFileExists "$0\bin\64bit\obs64.exe" 0 +2
        StrCpy $ObsDir $0
    ${EndIf}
  ${EndIf}

  SetRegView 64
  ${If} $ObsDir == ""
    StrCpy $ObsDir "$PROGRAMFILES64\obs-studio"
  ${EndIf}
FunctionEnd

Function ValidateObsDirectory
  IfFileExists "$ObsDir\bin\64bit\obs64.exe" obs_directory_valid
    Push "0"
    Return
  obs_directory_valid:
    Push "1"
FunctionEnd

Function BrowseForObsDirectory
  ${NSD_GetText} $ObsPathField $ObsDir
  nsDialogs::SelectFolderDialog "Select the OBS Studio folder" "$ObsDir"
  Pop $0
  ${If} $0 != "error"
    StrCpy $ObsDir $0
    ${NSD_SetText} $ObsPathField $ObsDir
    Call UpdateObsStatus
  ${EndIf}
FunctionEnd

Function UpdateObsStatus
  ${NSD_GetText} $ObsPathField $ObsDir
  Call ValidateObsDirectory
  Pop $0
  ${If} $0 == "1"
    ${NSD_SetText} $ObsStatusLabel "OBS Studio was found. Only the RearSilver OBS plugin will be installed here."
  ${Else}
    ${NSD_SetText} $ObsStatusLabel "OBS Studio was not found in this folder. Choose the folder containing bin\64bit\obs64.exe."
  ${EndIf}
FunctionEnd

Function ObsPageCreate
  StrCmp $UpdateHandoff "1" 0 +2
    Abort
  IfSilent 0 +2
    Abort

  !insertmacro MUI_HEADER_TEXT "Locate OBS Studio" "Choose where the RearSilver OBS plugin will be installed"
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}

  ${NSD_CreateLabel} 0 0 100% 28u "Setup detected the OBS Studio folder below. This location is used only for the OBS plugin; the Control Hub remains in the Suite destination selected on the previous page."
  Pop $0
  ${NSD_CreateText} 0 38u 78% 13u "$ObsDir"
  Pop $ObsPathField
  ${NSD_OnChange} $ObsPathField UpdateObsStatus
  ${NSD_CreateBrowseButton} 82% 37u 18% 15u "Browse..."
  Pop $0
  ${NSD_OnClick} $0 BrowseForObsDirectory
  ${NSD_CreateLabel} 0 59u 100% 26u ""
  Pop $ObsStatusLabel
  Call UpdateObsStatus
  nsDialogs::Show
FunctionEnd

Function ObsPageLeave
  ${NSD_GetText} $ObsPathField $ObsDir
  Call ValidateObsDirectory
  Pop $0
  ${If} $0 != "1"
    MessageBox MB_ICONSTOP|MB_OK "OBS Studio could not be found at:$\r$\n$ObsDir$\r$\n$\r$\nChoose the main OBS Studio folder containing bin\64bit\obs64.exe. The Control Hub installation folder is configured separately."
    Abort
  ${EndIf}
FunctionEnd

Function InstallSummaryPageCreate
  StrCmp $UpdateHandoff "1" 0 +2
    Abort
  IfSilent 0 +2
    Abort

  !insertmacro MUI_HEADER_TEXT "Confirm install locations" "The Suite and OBS plugin use separate destinations"
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}

  ${NSD_CreateLabel} 0 0 100% 12u "Control Hub and Suite files:"
  Pop $0
  ${NSD_CreateText} 0 15u 100% 13u "$INSTDIR"
  Pop $0
  SendMessage $0 ${EM_SETREADONLY} 1 0
  ${NSD_CreateLabel} 0 42u 100% 12u "RearSilver OBS plugin:"
  Pop $0
  ${NSD_CreateText} 0 57u 100% 13u "$ObsDir"
  Pop $0
  SendMessage $0 ${EM_SETREADONLY} 1 0
  ${NSD_CreateLabel} 0 82u 100% 22u "Click Back to change either location, or Install to continue."
  Pop $0
  nsDialogs::Show
FunctionEnd

Function .onInit
  SetRegView 64
  StrCpy $UpdateHandoff "0"
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/UPDATEHANDOFF" $1
  IfErrors +2 0
    StrCpy $UpdateHandoff "1"
  Call FindObsDirectory

  ; Interactive installs correct an undetected location on the OBS page.
  ; Silent updater handoffs cannot prompt, so they require a valid saved or
  ; automatically detected OBS location before any files are changed.
  IfSilent 0 init_complete
  Call ValidateObsDirectory
  Pop $0
  ${If} $0 != "1"
    MessageBox MB_ICONSTOP|MB_OK "OBS Studio could not be found at $ObsDir. Re-run the full RearSilver Stream Suite installer to select the correct OBS Studio folder."
    SetErrorLevel 2
    Abort
  ${EndIf}
  init_complete:
FunctionEnd

Function FinishPagePre
  ; The external updater restores the Hub and OBS after an update. Skip the
  ; normal Finish page so it cannot offer a second OBS launch.
  StrCmp $UpdateHandoff "1" 0 +2
    Abort
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
  StrCpy $3 "1"
  ReadRegDWORD $0 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" "Installed"
  ReadRegStr $1 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" "Version"
  ${If} $0 == 1
  ${AndIf} $1 != ""
  ${AndIf} "${RS_VC_RUNTIME_MIN_VERSION}" != "0"
    StrCpy $2 $1 1
    ${If} $2 == "v"
      StrCpy $1 $1 "" 1
    ${EndIf}
    ${VersionCompare} $1 "${RS_VC_RUNTIME_MIN_VERSION}" $2
    ${If} $2 == 0
    ${OrIf} $2 == 1
      StrCpy $3 "0"
    ${EndIf}
  ${EndIf}
  ${If} $3 == "1"
  DetailPrint "Extracting Microsoft Visual C++ installer..."
  File "/oname=$PLUGINSDIR\vc_redist.x64.exe" "${RS_PREREQUISITE_ROOT}\vc_redist.x64.exe"
  DetailPrint "Installing Microsoft Visual C++ Runtime; please wait..."
  ExecWait '"$PLUGINSDIR\vc_redist.x64.exe" /install /quiet /norestart' $0
  ${If} $0 != 0
  ${AndIf} $0 != 1638
  ${AndIf} $0 != 3010
    MessageBox MB_ICONSTOP|MB_OK "Microsoft Visual C++ Runtime setup failed with code $0. RearSilver Stream Suite was not installed."
    Abort
  ${EndIf}
  ${Else}
    DetailPrint "Microsoft Visual C++ Runtime $1 is already suitable; skipping installation."
  ${EndIf}

  DetailPrint "Checking Microsoft Edge WebView2 Runtime..."
  Call HasWebView2Runtime
  Pop $1
  ${If} $1 != "1"
    DetailPrint "Extracting and installing Microsoft Edge WebView2 Runtime; this may take a few minutes..."
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
  ${Else}
    DetailPrint "Microsoft Edge WebView2 Runtime is already installed."
  ${EndIf}
FunctionEnd

Section "RearSilver Stream Suite" MainSection
  SetShellVarContext all

  ; Revalidate immediately before installation so silent and command-line
  ; installs can never place plugin files in an arbitrary folder.
  Call ValidateObsDirectory
  Pop $0
  ${If} $0 != "1"
    MessageBox MB_ICONSTOP|MB_OK "OBS Studio could not be found at $ObsDir. RearSilver Stream Suite was not installed."
    SetErrorLevel 2
    Abort
  ${EndIf}

  ; Preserve the installation identity on upgrade. A missing installation
  ; gets a new identity even when the user's saved settings still exist.
  ReadRegStr $InstallInstance HKLM "${PRODUCT_REG_KEY}" "InstallInstance"
  IfFileExists "$INSTDIR\Control Hub\RearSilver-Stream-Suite-Control-Hub.exe" 0 fresh_install_identity
  StrCmp $InstallInstance "" fresh_install_identity install_identity_ready
  fresh_install_identity:
    ${GetTime} "" "L" $0 $1 $2 $3 $4 $5 $6
    StrCpy $InstallInstance "$2$1$0-$4$5$6"
  install_identity_ready:
  Call InstallPrerequisites

  ; Build inputs are checked by build-installer.ps1 and the File instructions
  ; at compile time. Installation uses embedded files, never developer paths.

  DetailPrint "Installing RearSilver OBS plugin and Control Hub files..."
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
  DetailPrint "Creating shortcuts and completing installation..."
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\RearSilver Stream Suite"
  CreateShortcut "$SMPROGRAMS\RearSilver Stream Suite\RearSilver Stream Suite - Control Hub.lnk" "$INSTDIR\Control Hub\RearSilver-Stream-Suite-Control-Hub.exe"
  CreateShortcut "$SMPROGRAMS\RearSilver Stream Suite\Uninstall RearSilver Stream Suite.lnk" "$INSTDIR\Uninstall.exe"

  WriteRegStr HKLM "${PRODUCT_REG_KEY}" "DisplayName" "${PRODUCT_NAME} (${RS_CHANNEL})"
  WriteRegStr HKLM "${PRODUCT_REG_KEY}" "InstallInstance" "$InstallInstance"
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

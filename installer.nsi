; ----------------------------------------
; RearSilver Stream Suite Installer
; Modern UI 2 – CLEAN BASE
; ----------------------------------------

Unicode True
!include "MUI2.nsh"

; ----------------------------------------
; Product Info
; ----------------------------------------
Name "RearSilver Stream Suite"
!define PRODUCT_VERSION "1.0.0"
!define PRODUCT_PUBLISHER "RearSilver Gaming"
!define PRODUCT_WEB_SITE "https://github.com/rearsilvergaming/RearSilver-Stream-Suite"

; ----------------------------------------
; Output + Icons (ORDER MATTERS)
; ----------------------------------------
!define MUI_ICON "installer.ico"

; Top header (right-side header area)
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "installer_header.bmp"

; Left-side big image (Welcome + Finish)
!define MUI_WELCOMEFINISHPAGE_BITMAP "welcome_banner.bmp"
; Optional (uncomment if you don’t want it stretched/cropped)
; !define MUI_WELCOMEFINISHPAGE_BITMAP_NOSTRETCH

OutFile "RearSilver Stream Suite.exe"

; ----------------------------------------
; Install target
; ----------------------------------------
InstallDir "$PROGRAMFILES64\obs-studio"
RequestExecutionLevel admin

; ----------------------------------------
; Project root (the installer script lives in this directory)
; ----------------------------------------
!define SOURCE_DIR "${__FILEDIR__}"

; ----------------------------------------
; UI Behaviour
; ----------------------------------------
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_NOAUTOCLOSE

; Header text (this DOES work)
!define MUI_HEADER_TEXT "RearSilver Stream Suite"
!define MUI_HEADER_SUBTEXT "Professional OBS Dock"

BrandingText "RearSilver Stream Suite | RearSilver Gaming"

; ----------------------------------------
; Pages (TEXT CONTENT)
; ----------------------------------------

; --- WELCOME: big bold heading (top) ---
!define MUI_WELCOMEPAGE_TITLE "Welcome to RearSilver Stream Suite"
!define MUI_WELCOMEPAGE_TITLE_3LINES

; --- WELCOME: paragraph body (under the title) ---
!define MUI_WELCOMEPAGE_TEXT "This setup will install RearSilver Stream Suite into OBS Studio.$\r$\n$\r$\n\
It is designed to feel calm, clean, and professional with quick-access \
enhancements and theme support.$\r$\n$\r$\n\
Click Next to continue."

; --- FINISH: big bold heading (top) ---
!define MUI_FINISHPAGE_TITLE "RearSilver Stream Suite is now installed"
!define MUI_FINISHPAGE_TITLE_3LINES

; --- FINISH: paragraph body ---
!define MUI_FINISHPAGE_TEXT "RearSilver Stream Suite has been successfully installed.$\r$\n$\r$\n\
Restart OBS to begin using the dock.$\r$\n$\r$\n\
We really hope you enjoy using RearSilver Stream Suite and if you do, tell your friends!"

; ----------------------------------------
; Launch OBS (KEEPING YOUR EXISTING SETUP)
; ----------------------------------------
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Launch OBS Studio"
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchOBS

; ----------------------------------------
; Pages (INSTALL ONLY)
; ----------------------------------------
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "License.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; ----------------------------------------
; Language (MUST be AFTER pages)
; ----------------------------------------
!insertmacro MUI_LANGUAGE "English"

; ----------------------------------------
; Install Section
; ----------------------------------------
Section "RearSilver Stream Suite"

  ; Ensure OBS plugin directory exists
  CreateDirectory "$INSTDIR\obs-plugins\64bit"

  ; Safety check
  IfFileExists "${SOURCE_DIR}\build_x64\RelWithDebInfo\RearSilver-Stream-Suite.dll" +2
    Abort "RearSilver-Stream-Suite.dll not found. Build the plugin first."

; Copy plugin
SetOutPath "$INSTDIR\obs-plugins\64bit"
File "${SOURCE_DIR}\build_x64\RelWithDebInfo\RearSilver-Stream-Suite.dll"

; Copy plugin localisation data
CreateDirectory "$INSTDIR\data\obs-plugins\RearSilver-Stream-Suite\locale"
SetOutPath "$INSTDIR\data\obs-plugins\RearSilver-Stream-Suite\locale"
File "${SOURCE_DIR}\data\locale\en-GB.ini"


SectionEnd

; ----------------------------------------
; Functions
; ----------------------------------------
Function LaunchOBS
  Exec '"$INSTDIR\bin\64bit\obs64.exe"'
FunctionEnd

!include "MUI2.nsh"
!include "FileFunc.nsh"
Unicode true
Name "NyaEdit ${VERSION}"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\NyaEdit"
RequestExecutionLevel user
SetCompressor zlib
!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"
Section "NyaEdit"
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\NyaEdit"
  CreateShortcut "$SMPROGRAMS\NyaEdit\NyaEdit.lnk" "$INSTDIR\NyaEdit.exe"
  CreateShortcut "$SMPROGRAMS\NyaEdit\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NyaEdit" "DisplayName" "NyaEdit"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NyaEdit" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NyaEdit" "DisplayIcon" "$INSTDIR\NyaEdit.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NyaEdit" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NyaEdit" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NyaEdit" "NoRepair" 1
SectionEnd
Section "Uninstall"
  Delete "$SMPROGRAMS\NyaEdit\NyaEdit.lnk"
  Delete "$SMPROGRAMS\NyaEdit\Uninstall.lnk"
  RMDir "$SMPROGRAMS\NyaEdit"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NyaEdit"
  Delete "$INSTDIR\Uninstall.exe"
  !include "${UNINSTALL_FILES}"
  RMDir "$INSTDIR"
SectionEnd


Name "CoinToss"

; The file to write
OutFile "toss install.exe"

; The default installation directory
InstallDir "$PROGRAMFILES\CoinToss"

; Request application privileges for Windows Vista
RequestExecutionLevel user

;--------------------------------

; Pages

Page directory
Page instfiles

;--------------------------------

; The stuff to install
Section "" ;No components page, name is not important

  ; Set output path to the installation directory.
  SetOutPath $INSTDIR
  
  ; Put file there
  	File ..\Release\newcoin.exe
	File ..\*.dll
	File "start CoinToss.bat"
	File newcoind.cfg
	File validators.txt
	File /r /x .git ..\..\nc-client\*.*
	

  
SectionEnd ; end the section

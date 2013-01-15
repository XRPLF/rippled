Name "Rippled"

; The file to write
OutFile "ripple install.exe"

; The default installation directory
InstallDir "$PROGRAMFILES\Rippled"

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
  	File ..\Release\rippled.exe
	File ..\*.dll
	;File "start rippled.bat"
	File rippled.cfg
	File validators.txt
	;File /r /x .git ..\..\nc-client\*.*
	
	CreateDirectory $INSTDIR\db
	

  
SectionEnd ; end the section

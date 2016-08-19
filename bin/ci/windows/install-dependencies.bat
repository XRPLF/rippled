if "%build%" == "scons" (
	rem Installing pip will install setuptools/easy_install.
	python get-pip.py

	rem Pip has some problems installing scons on windows so we use easy install.
	rem - easy_install scons
	rem Workaround
	easy_install https://pypi.python.org/packages/source/S/SCons/scons-2.5.0.tar.gz#md5=bda5530a70a41a7831d83c8b191c021e

	rem Scons has problems with parallel builds on windows without pywin32.
	easy_install pywin32-220.win-amd64-py2.7.exe
	rem (easy_install can do headless installs of .exe wizards)
)
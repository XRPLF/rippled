## To run these commands as a script, execution policy must be relaxed:
#set-executionpolicy remotesigned
## If running in admin powershell simply run:
Set-ExecutionPolicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
choco install -y python3
choco install -y git conan
choco install -y cmake.install --installargs '"ADD_CMAKE_TO_PATH=System"'

## These workloads depend on and will install visualstudio<year>buildtools
choco install -y visualstudio2022-workload-vctools visualstudio2019-workload-vctools
## and for the IDEs *Be prepared to wait!*
choco install -y visualstudio2019community
choco install -y visualstudio2019-workload-nativedesktop
choco install -y visualstudio2022community
choco install -y visualstudio2022-workload-nativedesktop
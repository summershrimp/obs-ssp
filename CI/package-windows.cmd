mkdir package
cd package

git rev-parse --short HEAD > package-version.txt
set /p PackageVersion=<package-version.txt
del package-version.txt

curl -L "https://github.com/imaginevision/libssp/raw/fa0affd3858049a7773995c38bb454f989e7c8f4/lib/win_x64_vs2017/libssp.dll" -f --retry 5 -o "..\release\obs-plugins\64bit\libssp.dll"

REM Package ZIP archive
7z a "obs-ssp-%PackageVersion%-Windows.zip" "..\release\*"

REM Build installer
iscc ..\installer\installer.iss /O. /F"obs-ssp-%PackageVersion%-Windows-Installer"

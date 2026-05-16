# 7zSF

![autobuild](https://github.com/chrdev/7zSF/actions/workflows/build.yaml/badge.svg)

A 7-Zip self extractor for special folders. For Windows XP and above, x86 and x64.  
Extract payload to special folders according to CSIDL, optionally run a command afterwards.  
A lightweight installer.

## Useage

Use CSIDL as payload folder names. CSIDL stands for "constant special item ID list". Each CSIDL represents a special folder.  
The CSIDL format is a 2-digits hex value. see [reference section](#reference).

If there is a "y" file in the payload root, it must be a UTF-16 LE encoded text file. Its content will be showed with Yes/No options.  
The user may choose "Yes" to extract, or "No" to cancel.

If there is a "z" file in the payload root, it must be a UTF-16 LE encoded text file. After the extraction, its content will be executed as a command.  
z should contain only one line, but trailing blank lines and spaces are permitted.

Below is a typical payload dir tree, aa, bb and cc stand for different CSIDL names:

    7z payload root
    ├───aa
    │   └───contents to be extracted to CSIDL aa special folder
    ├───bb
    │   └───contents to be extracted to CSIDL bb special folder
	├───cc
    │   └───contents to be extracted to CSIDL cc special folder
    ├───y : (UTF-16 LE text, prompt before the extraction, optional)
    └───z : (UTF-16 LE text, one-liner command, optional, runs after extraction)

Like other 7z sfx modules, combine the sfx and payload via:
```
copy /b 7zSF.sfx + payload.7z my-installer.exe
```

## Examples

### 1. Drop a file to desktop

CSIDL for desktop is "00".

```
mkdir 00
echo hello 7zSF> 00\hello-7zSF.txt

7z a payload.7z 00
copy /b 7zSF.sfx + payload.7z hello.exe
```

Run hello.exe, now we have hello-7zSF.txt file on desktop.

### 2. Drop a file to desktop, then run a command

Following the above example, open notepad and paste:

```
notepad %USERPROFILE%\desktop\hello-7zSF.txt
```

Save as z, (NOT z.txt), the encoding must be UTF-16 LE.

```
7z a payload.7z 00 z
copy /b 7zSF.sfx + payload.7z hello.exe
```

Run hello.exe, now we have hello-7zSF.txt file on desktop. And notepad opened with it.  
Note: %USERPROFILE%\desktop is not guaranteed to be the Desktop folder, it's used here for demonstration purpose only.

### 3. Repack a self-hosted RustDesk client:

First run once the official RustDesk client, so that we have all the files.

CSIDL for roaming AppData is "1a".  
CSIDL for local AppData is "1c".  
Organize our payload folder:

```
payload root
├───1a
│   └───RustDesk : (copied from %APPDATA%)
│       └───config
│               RustDesk2.toml
│               
├───1c
│   └───rustdesk : (copied from %LOCALAPPDATA%)
│       │   rustdesk-qs.exe : (renamed from rustdesk.exe, so that it asks for elevation)
│       │   install.js
│       │   uninstall.js
│       │   ... other contents
└-──z : (command to run after extraction)
```

```
1a\RustDesk\config\RustDesk2.toml
---------------------------------
[options]
custom-rendezvous-server = 'www.example.com'
key = 'my-rustdesk-key-to-connect-to-my-server'
```

```
1c\rustdesk\install.js
----------------------
// Tested on Windows 11, may also work on 10.
var exePath = "%LOCALAPPDATA%\\rustdesk\\rustdesk-qs.exe";

var sh = new ActiveXObject("WScript.Shell");
var fs = new ActiveXObject("Scripting.FileSystemObject");

var scut = sh.CreateShortcut(sh.SpecialFolders("Desktop") + "\\RustDesk.lnk");
scut.TargetPath = exePath;
scut.Save();

var groupName = sh.SpecialFolders("StartMenu") + "\\RustDesk";
try { fs.CreateFolder(groupName); } catch (e) { }
scut = sh.CreateShortcut(groupName + "\\RustDesk.lnk");
scut.TargetPath = exePath;
scut.Save();

scut = sh.CreateShortcut(groupName + "\\Uninstall.lnk");
scut.TargetPath = "wscript.exe";
scut.Arguments = "//nologo %LOCALAPPDATA%\\rustdesk\\uninstall.js";
scut.IconLocation = "%SYSTEMROOT%\\System32\\SHELL32.dll,131";
scut.Save();

sh.Run(exePath);
```

```
1c\rustdesk\uninstall.js
------------------------
// Test on Windows 11, may also work on 10.
var deletionTimeoutInSec = 8;

var sh = new ActiveXObject("WScript.Shell");
var fs = new ActiveXObject("Scripting.FileSystemObject");

if (6 != sh.Popup("Uninstall RustDesk?\nBefore clicking \"Yes\", make sure RustDesk is NOT running.\nOtherwise uninstallation will not be clean.", 0, "RustDesk", 4 + 48 + 256)) WSH.Quit();

function forceDeleteFolder(path) {
	for (var i = 0; fs.FolderExists(path) && i < deletionTimeoutInSec * 2; ++i) {
		try { fs.DeleteFolder(path, true); break; } catch (e) { }
		WSH.Sleep(500);
	}
}

forceDeleteFolder(sh.ExpandEnvironmentStrings("%LOCALAPPDATA%\\rustdesk"));
forceDeleteFolder(sh.ExpandEnvironmentStrings("%APPDATA%\\rustdesk"));
forceDeleteFolder(sh.SpecialFolders("StartMenu") + "\\RustDesk");
try{ fs.DeleteFile(sh.SpecialFolders("Desktop") + "\\RustDesk.lnk", true); } catch (e) { }
```

```
z : UTF-16 LE text
--------
wscript.exe //nologo %LOCALAPPDATA%\rustdesk\install.js
```

Now we pack it.

```
7z a payload.7z 1a 1c z
copy /b 7zSF64.sfx + payload.7z my-rustdesk.exe
```

## Reference

These CSIDL names/values and their meanings are copied from ShlObj_core.h.  
They are listed here for reference purpose only. All of them may not work in practice.

    Name      Meaning or typical location
    ------    ---------------------------
    00        <desktop>
    02        Start Menu\Programs
    04        My Computer\Printers
    05        My Documents
    06        <user name>\Favorites
    07        Start Menu\Programs\Startup
    08        <user name>\Recent
    09        <user name>\SendTo
    0b        <user name>\Start Menu
    0d        "My Music" folder
    0e        "My Videos" folder
    10        <user name>\Desktop
    14        windows\fonts
    15        TEMPLATES
    16        All Users\Start Menu
    17        All Users\Start Menu\Programs
    18        All Users\Startup
    19        All Users\Desktop
    1a        user application data (roaming)
    1c        user local applicaiton data (non roaming)
    1d        non localized startup
    1e        non localized common startup
    1f        common favorites 
    21        internet cookies
    23        All Users Application Data
    24        GetWindowsDirectory()
    25        GetSystemDirectory()
    26        C:\Program Files
    27        C:\Program Files\My Pictures
    28        USERPROFILE
    2b        C:\Program Files\Common
    2d        All Users\Templates
    2e        All Users\Documents
    2f        All Users\Start Menu\Programs\Administrative Tools
    30        <user name>\Start Menu\Programs\Administrative Tools
    31        Network and Dial-up Connections
    35        All Users\My Music
    36        All Users\My Pictures
    37        All Users\My Video
    38        Resource Direcotry
    39        Localized Resource Direcotry
    3a        Links to All Users OEM specific apps
    3b        USERPROFILE\Local Settings\Application Data\Microsoft\CD Burning

## Development

Prerequisite:

* Modern Visual Studio, eg. communiy 2026, with C (CPP), nmake, and Windows SDK
* 7-Zip LZMA SDK
* 7-Zip (for packing release)
* upx (for packing release)

Steps:

1. Put this project dir in LZMA-SDK\C\Util, besides SfxSetup (This project doesn't need SfxSetup).
2. Open a normal command prompt, NOT command prompt for VS.
3. run build.bat within the project folder.

build.bat will find the installed VS, bulid and pack the release.  
Resultant files are in bulid\release.

This project links to the good? old msvcrt.dll to reduce binary size and run on Windows XP.

A VS2026 solution is provided to make debugging easier, it's not equivalent to the makefile.

## LICENSE
Public Domain

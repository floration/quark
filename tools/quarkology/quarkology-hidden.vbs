' Quarkology without Electron: runs quarkology.cmd --edge with no console window (window style 0).
' The "Quarkology" desktop shortcut targets this (wscript.exe) when setup.ps1 ran with -NoElectron.
' Installed by tools\quarkology\setup.ps1 into Quark\quarkology\.
Option Explicit
Dim sh, fso, here
Set sh = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
here = fso.GetParentFolderName(WScript.ScriptFullName)
sh.CurrentDirectory = here
sh.Run """" & here & "\quarkology.cmd"" --edge", 0, False

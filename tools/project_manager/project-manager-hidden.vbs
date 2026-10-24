' Quark Project without Electron: runs project-manager.cmd --edge with no console window (window style 0).
' The "Quark Project" desktop shortcut targets this (wscript.exe) when setup.ps1 ran with -NoElectron.
' Installed by tools\project_manager\setup.ps1 into Quark\project-manager\.
Option Explicit
Dim sh, fso, here
Set sh = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
here = fso.GetParentFolderName(WScript.ScriptFullName)
sh.CurrentDirectory = here
sh.Run """" & here & "\project-manager.cmd"" --edge", 0, False

' Quark Project is now Quarkology (2026-10-27): runs Quark\quarkology\quarkology-hidden.vbs (the Edge window, no
' console). The old app's self-update put this in Quark\project-manager\ (tools/project_manager/, kept for one release).
Option Explicit
Dim sh, fso, here, newVbs
Set sh = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
here = fso.GetParentFolderName(WScript.ScriptFullName)
newVbs = fso.BuildPath(fso.GetParentFolderName(here), "quarkology\quarkology-hidden.vbs")
If fso.FileExists(newVbs) Then
  sh.Run "wscript.exe """ & newVbs & """", 0, False
Else
  sh.CurrentDirectory = here
  sh.Run """" & here & "\project-manager.cmd"" --edge", 0, False
End If

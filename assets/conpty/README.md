# ConPTY (Windows)

`conpty.dll` and `OpenConsole.exe` come from the NuGet package
[Microsoft.Windows.Console.ConPTY](https://www.nuget.org/packages/Microsoft.Windows.Console.ConPTY)
version 1.25.260930003 (Windows Terminal, MIT licence, © Microsoft Corporation).

The ConPTY built into Windows does not pass queries such as `OSC 11 ; ?` to the
terminal. This newer one does, so apps like Claude Code can find the background
colour. glypht loads `conpty.dll` from the folder of `glypht.exe` and falls back
to the ConPTY of Windows if the file is missing. `OpenConsole.exe` must be in the
same folder.

To update, replace the files for each architecture from the new package
(`runtimes/win-<arch>/native/conpty.dll` and
`build/native/runtimes/<arch>/OpenConsole.exe`) and change the version above.

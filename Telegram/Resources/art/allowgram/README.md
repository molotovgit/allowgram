The Allowgram mark is an original teal speech bubble with a white check.
`mark.svg` is the editable source. Existing Telegram artwork stays in its
original paths; the client resource aliases select the Allowgram assets.

`generate_assets.cpp` uses Qt Core, Gui, and Svg to rasterize the source. It
writes two transparent 256 x 256 PNGs, a Windows ICO containing 16, 20, 24,
32, 40, 48, 64, 128, and 256 pixel PNG frames, and three tray SVGs. The tray
check is transparent so the existing Windows light/dark recoloring works.
Unread and muted indicator dots retain their existing colors.

To regenerate, run these commands from this directory in a Visual Studio
2022 x64 Native Tools Command Prompt, replacing the Qt path as needed:

```bat
set "ALLOWGRAM_QT=C:\Qt\6.8.3\msvc2022_64"
cl /nologo /std:c++20 /EHsc /MD /utf-8 /Zc:__cplusplus /I"%ALLOWGRAM_QT%\include" generate_assets.cpp /Fegenerate_assets.exe /link /LIBPATH:"%ALLOWGRAM_QT%\lib" Qt6Core.lib Qt6Gui.lib Qt6Svg.lib
set "PATH=%ALLOWGRAM_QT%\bin;%PATH%"
generate_assets.exe .
```

The generated PNG and ICO files are checked in; the application build does
not need to run this utility. The SVG and utility follow the repository's
GPL-3.0 license.

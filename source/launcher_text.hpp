#pragma once

// launcher text
constexpr wchar_t InstallText[] = LR"(Install DDrawCompat

The following changes will be made:

- Existing DDraw.dll, D3DImm.dll, setup.ini and %CONFIG% -> "old" folder
- Record which files were absent, so restoration can remove added files
- Replace DDraw.dll with DDrawCompat
- Remove D3DImm.dll from root folder
- Set windowmode=0 under [SETTINGS] in setup.ini
- Set window=0 under [STARTUP] in setup.ini
- Create %CONFIG% for borderless fullscreen

These changes remain installed after closing the launcher/game and DO NOT modify the original %EXE%

DLL and setup.ini changes apply to all games in the same folder.

You can always restore the changes in the launcher!

Continue? [y/N]
)";

constexpr wchar_t RestoreText[] = LR"(Restore original files

The following changes will be made:

- Restore DDraw.dll, D3DImm.dll, setup.ini and recorded DDrawCompat settings from "old"
- Remove managed files that were originally absent

This restores the recorded changes for all games in this folder.

You can reinstall later if you change your mind!

Continue? [y/N]
)";

constexpr wchar_t CommandHelp[] = LR"(T3DGM Compatibility Launcher 0.4.0
Usage: T3DGM-Compatibility.exe [--check] [editor.exe|game.exe]

Drag an executable onto the launcher, or specify its filename on the command line.
With no filename, the launcher looks for t3dgm.exe beside it or one folder above.
--check identifies the native runtime and checks local prerequisites without launching.
A 32-bit d3drm.dll must already be beside the selected executable.
)";

constexpr wchar_t Recommendation[] = LR"(T3DGM will not display correctly on most setups without DDrawCompat! It's strongly recommended you install it.
If you change your mind later, you can restore the original files in this launcher.
)";

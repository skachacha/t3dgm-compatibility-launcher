#pragma once

// launcher text
constexpr wchar_t InstallText[] = LR"(Install DDrawCompat

The following changes will be made:

- DDraw.dll, D3DImm.dll, setup.ini -> "old" folder
- Replace DDraw.dll with DDrawCompat
- Remove D3DImm.dll from root folder
- Set windowmode=0 under [SETTINGS] in setup.ini
- Set window=0 under [STARTUP] in setup.ini
- Create DDrawCompat-t3dgm.ini for borderless fullscreen

These changes remain installed after closing the launcher/T3DGM and DO NOT modify the original t3dgm.exe

You can always restore the changes in the launcher!

Continue? [y/N]
)";

constexpr wchar_t RestoreText[] = LR"(Restore original files

The following changes will be made:

- DDraw.dll, D3DImm.dll, setup.ini restored from "old" folder
- Remove DDrawCompat-t3dgm.ini

You can reinstall later if you change your mind!

Continue? [y/N]
)";

constexpr wchar_t Recommendation[] = LR"(T3DGM will not display correctly on most setups without DDrawCompat! It's strongly recommended you install it.
If you change your mind later, you can restore the original files in this launcher.
)";

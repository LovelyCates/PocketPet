# Validation

The Windows build runs 24 checks covering:

- Throw velocity, speed limits, dissipating bounces, settling and frame-rate stability.
- Negative monitor coordinates and collision containment.
- Animation timing, fallback, looping and 16-direction gaze indexing.
- PNG v1 and WebP v2 loading, transparency and premultiplied alpha.
- Invalid manifests, image dimensions, frame indices and escaping asset paths.
- Importing external packages without changing their sources or overwriting existing pets.
- Restoring local selections and handling missing selections without built-in artwork.
- Replacing artwork independently of the executable.
- Isolating sprite frames during scaling/rotation to prevent adjacent-frame borders.
- Autonomous poses, rest/wake transitions and interaction precedence.
- Per-role green-fringe display cleanup while preserving interior colours.
- Manual forms remaining selected until automatic mode is explicitly restored.
- Creating a discoverable manifest when importing a bare standard atlas.

Run locally using PowerShell 7 and Visual Studio C++ Build Tools:

```powershell
./build.ps1 -Test
```

The GitHub Actions Windows workflow builds from a fresh checkout, runs these checks and packages the portable executable with its external `pets` directory.

Manual desktop checks covered dragging/throwing, context menus, character changes, holding poses beyond the earlier timeout, temporary interaction returning to the held pose, automatic rest/wake, settings restoration and moving a portable installation to another directory. Physical multi-monitor DPI transitions have not been exhaustively tested.

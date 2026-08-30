# SpectralFix

SpectralFix smooths the jagged, pixelated glow around Final Fantasy XI avatars,
certain Trusts, mounts, and other glowing characters at modern resolutions.

It restores the soft aura without changing character files, replacing textures,
or requiring anything from the game server. Install it once, and it loads with
Ashita whenever the game starts.

## Before and after

<table>
  <tr>
    <th>Original jagged aura</th>
    <th>Default SpectralFix aura</th>
    <th>Higher spread setting</th>
  </tr>
  <tr>
    <td><img src="docs/assets/original-jagged.png" alt="Carbuncle with the original jagged aura" width="280"></td>
    <td><img src="docs/assets/spectralfix-default.png" alt="Carbuncle with the SpectralFix default aura" width="280"></td>
    <td><img src="docs/assets/spectralfix-high-spread.png" alt="Carbuncle with a wider SpectralFix aura" width="280"></td>
  </tr>
</table>

## Download the correct build

The release page provides two downloads per release, one per Ashita plugin
interface:

- `SpectralFix-<version>-Ashita-4.16.zip`
- `SpectralFix-<version>-Ashita-4.30.zip`

The `4.16` and `4.30` labels are Ashita **plugin interface** versions, not
necessarily the version shown by the launcher. If you choose the wrong package,
Ashita will refuse to load it rather than damage anything; try the other package
or include your Ashita log when asking which one you need.

## Installation

> **Required:** FFXI's Blur Effect must remain enabled while using SpectralFix.
> If it is disabled, run `/localsettings blureffect on`. Current tested clients
> recover immediately; if the corrected aura still does not appear, fully exit
> FFXI and relaunch once.

1. Exit Final Fantasy XI completely.
2. Download and extract the package matching your Ashita interface.
3. Copy `spectralfix.dll` into Ashita's `plugins` folder.
4. Add `/load spectralfix` near the top of the Ashita startup script, before
   ordinary plugins and addons.
5. Launch the game normally.

Do not wait until after login and load SpectralFix manually. It must load from
the startup script so it is present when FFXI creates the aura resources.

Do not unload or reload SpectralFix while the game is running. Exit the entire
client before replacing or removing the DLL.

## Why this is a plugin instead of an addon

Ashita addons are well suited to commands, interface changes, and normal game
events, but this problem happens earlier and deeper in FFXI's graphics system.
SpectralFix must be present when the game creates the aura and must correct how
that aura is drawn, which requires a native plugin.

Once SpectralFix enlarges the aura, it cannot safely shrink that live graphics
resource again. Unloading the plugin would remove the matching corrections while
the enlarged aura is still in use, which can place a large animated copy in the
corner of the screen. Fully exiting the game clears those graphics resources and
allows the next launch to start cleanly.

## Commands

- `/spectralfix` or `/spectralfix help` shows the available settings.
- `/spectralfix spread <1.0-16.0>` changes how far the aura spreads. The default is `2.0`.
- `/spectralfix opacity <0-100>` changes the strength of the soft outer glow.
- `/spectralfix composite opacity <0-100>` changes the strength of the inner glow. The default is `25`.
- `/spectralfix target <medium|high|ultra>` changes aura quality after a full restart. `high` is recommended.

Use `stock` instead of a number to restore the original opacity behavior, or use
`auto` instead of a spread number to let SpectralFix scale it automatically.
Changes are saved for future launches.

## If something goes wrong

If SpectralFix loads but the aura is missing or remains jagged, first run
`/localsettings blureffect on`. The corrected aura normally appears immediately.
If it does not, fully exit FFXI, relaunch once, and try the aura again.

Ashita does not expose this live setting to plugins, so SpectralFix does not try
to detect it or display an automatic warning. Check it manually with the command
above when troubleshooting.

Also confirm that `/load spectralfix` is near the top of the startup script. If
you loaded it manually after login, fully exit FFXI and let the startup script
load it during the next launch.

Run `/spectralfix status` and include the displayed information when reporting the
problem. SpectralFix also keeps a small troubleshooting log in
`ashita/logs/spectralfix/`.

## License

SpectralFix is copyright (C) 2026 KraturLabs and licensed under
[GPL-3.0-only](LICENSE). Third-party notices are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Final Fantasy XI is a trademark of Square Enix. This independent community
project is not affiliated with or endorsed by Square Enix or the other projects
named above.

# YouTube Music

Host-controlled YouTube music with guest catch-up and local volume.

## Install

Install [Moon Sweepers Mod Loader](https://github.com/swooping-mods/moon-sweepers-mod-loader) first.

Download [youtube-music-0.1.1.msmod](content/youtube-music-0.1.1.msmod), import it with **Install mod package**, then select the mod and its required frameworks.

## Controls

- Every player needs YouTube Music and its music helper.
- Use the music panel to select a YouTube track. The crew host controls playback.

## Compatibility

Version 0.1.1. Targets the pinned Steam Windows x64 IL2CPP build of Moon Sweepers (Steam app 5335510). On Linux, the game runs through Proton. Experimental: back up saves before using save-affecting mods.

## Build

Run `python3 build.py --check` to verify source hashes, then `python3 build.py` to write a build under `.local/build/`. Requires installed Clang, lld-link and llvm-dlltool.

## Credits

See [CREDITS.txt](CREDITS.txt) and [third-party notices](vendored/THIRD_PARTY_NOTICES.md).

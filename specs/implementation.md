# YouTube Music: behavior and implementation

Describes the exported implementation **0.1.1**. The linked source and metadata define this version; gameplay qualification is a separate requirement.

## Behavior

The crew host selects a YouTube track in the Python helper/panel and controls play, pause, resume, seek and stop; volume is local. Every peer runs its own mod/helper and prepares audio locally. Native timeline snapshots authenticate the Steam host/member and synchronize late joins with request/response timestamps; guests pause after three seconds without a valid snapshot. Drift correction uses the game’s retained seconds-based AudioSource accessors.

## State and lifecycle

No music save data is written. World/crew changes reset audio and local command generation so obsolete helper commands cannot affect a new session. A local file bridge publishes status/acknowledgements; helpers serialize unacknowledged commands. Track/cache preparation uses installed yt-dlp/FFmpeg, bounded local PCM decode and cancellation; the optional panel uses existing PySide6. The mod does not install/update those tools.

## Integration and configuration

No additional feature-mod runtime dependency is declared. Included shared headers are source reuse, not extra DLL dependencies.

The [module metadata](../source/mods/youtube-music/mod.json) owns the version and declared integration contract. Native modules start at `MoonLab_ModStart`. Game/Unity integrations target the pinned Steam Windows x64 IL2CPP build.

## Implementation sources

- [mod.c](../source/mods/youtube-music/mod.c)
- [core.h](../source/mods/youtube-music/core.h)
- [transport.h](../source/mods/youtube-music/transport.h)
- [music.py](../source/mods/youtube-music/music.py)
- [panel.py](../source/mods/youtube-music/panel.py)

## Verification boundary

No host migration or all-player readiness barrier. Local audio/network failure can interrupt one peer; timing thresholds are not a measured live sync guarantee. Live extraction/playback, rendered panel and real co-op require separate checks.

`python3 build.py --check` verifies the exported source hashes. It does not execute the mod or qualify the behaviors above. Installation and independent build prerequisites are in the [README](../README.md).

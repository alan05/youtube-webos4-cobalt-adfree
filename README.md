# YouTube Cobalt for LG webOS 4

Experimental, unofficial Cobalt-based YouTube clients for
older LG webOS televisions.

This project builds upon
[RF1705/youtube-webos-cobalt-adfree](https://github.com/RF1705/youtube-webos-cobalt-adfree).

It is not affiliated with Google, YouTube, LG or webOS.

## Application variants

| Variant | TV launcher name | Application ID |
| --- | --- | --- |
| Standard | YouTube | `com.alan05.youtube.cobalt.webos4` |
| Kids | YouTube Kids | `com.alan05.youtubekids.cobalt.webos4` |

Both variants share the same Cobalt runtime.

The Kids variant is not yet validated for public release.
Its separate profile, sign-in behaviour and parental controls
require additional testing.

## Verified hardware baseline

- Television: LG 75UK6500PLA
- Firmware: 05.50.70
- webOS: 4.4.3-22
- Cobalt source: 23.lts.6
- Playback backend: experimental legacy LG ACB integration

A separately installed personal test build has demonstrated
reported 2160p playback, SponsorBlock functionality and
extended playback without sustained audio drift.

This does not establish compatibility with all webOS 4 devices.

## Known limitations

- Seeking can take approximately 14–17 seconds.
- Intermittent initial audio/video desynchronisation was observed.
- Public application IDs have not yet passed device testing.
- Kids profile restrictions are not yet verified.

## Build and release status

Development only. No verified public IPK is available yet.

The packaging system requires an explicit application profile.
Public releases must pass source, dependency, package integrity,
installation and regression checks.

## Upstream and attribution

Based on RF1705's YouTube webOS Cobalt AdFree and its
contributors. Original Git history is retained.

The original upstream README is preserved in
[docs/UPSTREAM_README.md](docs/UPSTREAM_README.md).

Reusable legacy webOS Cobalt/Starboard engineering is planned
for the separate `alan05/webos-cobalt-legacy` project.

## Licensing

Licensing and redistribution permissions for all project
components and bundled runtime libraries are under review.
The webapp package declares GPL-3.0; this declaration is
not treated as a blanket licence for the entire repository.

No public binary release should be published until the
applicable permissions and notices are verified.

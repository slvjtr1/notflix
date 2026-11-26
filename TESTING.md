# Native webOS release checks

This fork uses Notflix TV 1.0.3 as the behavior reference. The native port is a preview. The table distinguishes functionality in the package from the tests performed on it.

## Feature coverage

| Area | Available in the native port | Coverage still open |
| --- | --- | --- |
| Home, catalogs and remote | Rotating featured titles, movie and series catalogs, poster grid, search, detail pages, episode selection, Back focus restoration, white neon focus | Expanded preview cards, full curated rows, Top 10 by country, cast/crew pages and all legacy layouts |
| Providers | Install and remove Stremio manifests, search advertised catalogs, retrieve metadata and sources, play compatible HTTP streams, import enabled addons from Nuvio | Required catalog filters, plugin runtimes, raw torrents, custom playback headers and full debrid compatibility |
| Playback | LG native media service, play/pause, seek, selectable audio and subtitles, SRT/WebVTT cues, direct trailers when provided | Full format matrix, adaptive streams on each firmware, HDR formats, advanced sync, night mode, sleep timer, reordering controls, YouTube-only trailers and next-episode automation |
| Library and profiles | Local profiles, saved titles, watch progress, local KIDS preset, four-digit parent PIN, unrated content hidden from KIDS | Cloud profile administration, customized rating ceilings, cross-device restrictions and full offline/online conflict handling |
| Nuvio | Device-code sign-in, credential refresh, explicit primary-profile sync, queued library/progress edits and addon import | Live account acceptance, cloud PIN entry, other cloud profiles and full account/settings migration |
| Phone | Paired search and remote controls over the home network, basic cast-to-title, QR code | Transfer of downloaded media from the phone and full episode/timestamp handoff |
| Watch parties | Create/join for the same title and episode, leave, host playback updates and follower synchronization through Nuvio snapshots | Multi-account acceptance, chat, real-time broadcast compatibility, host changes and stale-room recovery |
| Downloads | Single direct-file background transfer, atomic completion and play from local storage | Storage quota management, resumable downloads, whole seasons and smart downloads |
| Language | 38 language choices including English, using upstream translations where a matching label exists | Per-language layout, complex-script fonts, right-to-left navigation and complete UI and metadata localization |
| Other upstream features | Pick a saved title at random | Full discovery filters, calendar/reminders, ambient mode, Trakt, Simkl, IntroDB, TMDB-specific enrichment and in-app updates |

## Automated checks

The GitHub workflow compiles the desktop build with AddressSanitizer and UndefinedBehaviorSanitizer. The core suite checks URL escaping, provider response handling, selection of supported stream URLs, atomic JSON persistence, age restrictions, PIN verification and subtitle timing. The navigation suite exercises focus restoration, keyboard entry, the player menu and seeking.

The HTTP integration fixture installs an add-on through the app UI, opens its catalog and title, saves the title, resolves a playable stream, records playback progress, returns home and searches for a string containing a slash and a space. Playback on the desktop uses a deterministic test adapter.

The ARM build uses the community webOS SDK. The packaging check reopens the IPK, checks its app ID and native manifest, verifies the executable bit and ELF payload, checks the required shared objects and test clip and rejects Android binaries. A SHA-256 accompanies every package.

## Physical TV checks

The reference set reports model **FIESTA50Q**, webOS SDK **7.5.4**, and firmware **04.51.80**. The first package installed but exited because its SDK image library requested an SDL symbol missing on this TV. The corrected build uses the TV’s own SDL image and font libraries and stayed running after launch.

**Settings → Run 15-minute TV benchmark** exercises cold and warm browsing, simulated held arrows, Home/Settings transitions and player overlays in three 60-second samples per scenario. Press Back to cancel. A developer can cold-launch the same test using the launch parameter `{"notflixBenchmark":true}`.

The timer spans consecutive completed SDL presentations. At 60 Hz, an interval that rounds to two display periods counts as one estimated missed refresh. The test records catalog counts and decoder position separately to show whether it actually exercised catalog loading and video. The report is written to `/media/developer/apps/usr/palm/data/tv.notflix.benchmark.tsv`. It contains timing data and playback-test diagnostics.

See the release notes for measurements and unresolved checks on the distributed build. Stable requires the named scenarios to pass the zero-miss target, verified playback and completed feature acceptance.


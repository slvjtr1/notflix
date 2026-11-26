# Notflix for LG webOS

A native C television client based on the Notflix 1.0.3 TV feature set. Navigate with the directional pad or Magic Remote pointer. Focused controls have a white outline and glow. The home screen features titles from Cinemeta. Install a compatible stream add-on to play titles in its catalog, or open a direct media URL.

## Install

Download the ARM IPK and SHA256SUMS from [the webOS releases](https://github.com/slvjtr1/notflix/releases). In LG’s Developer Mode app, enable Developer Mode and Key Server, then connect the TV in webOS Dev Manager. Select **Install** and choose the IPK. The CLI equivalent for a configured device is:

```sh
ares-install -d tv tv.notflix.native_0.1.2_arm.ipk
ares-launch -d tv tv.notflix.native
```

The app ID is `tv.notflix.native`. See [feature coverage and testing](TESTING.md) for the tested firmware, implementation gaps and performance measurements.

## First use

- **Settings → Addons** accepts a Stremio manifest URL, including user-configured providers. Cinemeta supplies catalog and title metadata. MPEG playback depends on a provider returning a direct HTTP(S) source in a format the TV decodes.
- **Settings → Playback test** opens a generated local clip. During playback press OK to reveal the controls. Choose audio, embedded subtitles, or an available SRT/WebVTT subtitle from an add-on.
- **Settings → Profiles** creates separate local libraries, downloads and watch histories. Creating the first KIDS profile sets a parent PIN. The local KIDS preset admits provider-rated titles for ages seven and under and hides titles of unknown age. Parent PIN verification is required to leave the KIDS profile.
- **Settings → Nuvio account** links the current adult profile with a phone code. **Sync primary profile** sends edits made after linking and imports the unlocked primary cloud profile’s library, progress and enabled add-on manifests. Disconnect clears local session credentials and queued edits.
- **Settings → Phone remote** displays a QR code for the local-network remote. Opening the link permits that phone to navigate and search. Disconnect the remote in that screen or switch profiles to end the session.
- **Watch parties** use a linked Nuvio account. Open the same title and episode on each device, then host or join from the player. Each viewer supplies their own playable source. Room updates exchange title identifiers and the playback position.
- **Downloads** saves a direct file in the TV’s app data directory. The download becomes available after the transfer completes. Streaming playlists require online playback.

## Build

The [native build workflow](.github/workflows/native.yml) pins the webOS SDK archive and verifies its SHA-256. It builds the app twice, runs desktop checks with AddressSanitizer and UndefinedBehaviorSanitizer, cross-compiles for ARM, and checks the IPK contents. Linux host build:

```sh
sudo apt-get install cmake ninja-build pkg-config libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev libcurl4-openssl-dev libssl-dev
cmake -S . -B build-host -G Ninja -DSANITIZE=ON
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

For TV builds, use the pinned SDK and packaging commands from the workflow. The app’s own screens, input, data handling and webOS player integration are C. The phone remote serves a small web page. The desktop player is a test double. The ARM build calls LG’s hardware media service.

## Credits

Notflix and NuvioTV are GPL-3.0 projects. This fork contains a newly written native TV interface and keeps upstream translations, fonts and credits. The firmware media adapter comes from Stremio Origin for webOS. See [component credits](licenses/COMPONENTS.md) and the [GPL license](LICENSE). Notflix is an independent project. Netflix is the owner of the launcher symbol. No Netflix subscription or Netflix catalog is included.


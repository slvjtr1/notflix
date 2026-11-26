# Component sources

- Notflix assets, translated interface labels and behavior baseline come from [DaoeZ/notflix 1.0.3](https://github.com/DaoeZ/notflix/tree/aee68011338b19104f5a7391233fdd180b2b4882). The inherited project license is GPL-3.0.
- The C adapter for LG playback and Luna services comes from [Stremio Origin for webOS](https://github.com/slvjtr1/webos/tree/1b05a4533a41fdcf1cd24b968e603d02b2e98c85). The native client supplies its own app ID, lifecycle and rendering.
- cJSON uses the MIT license in `third_party/cjson/LICENSE`. The client uses per-thread parser error state.
- The paired HTTP remote uses [CivetWeb 1.16](https://github.com/civetweb/civetweb/tree/d7ba35bbb649209c66e582d5a0244ba988a15159). The built-in file server and CGI are disabled. The accompanying license is in `CivetWeb.txt`.
- QR rendering uses [Project Nayuki’s C library](https://github.com/nayuki/QR-Code-generator/tree/720f62bddb7226106071d4728c292cb1df519ceb/c), MIT.
- Inter and Bebas Neue use the SIL Open Font License. Their notices are included here.
- The launcher uses Netflix’s red ribbon N artwork from [Wikimedia Commons](https://commons.wikimedia.org/wiki/File:Netflix_2016_N_logo.svg). The original SVG is preserved in `branding/launcher-mark.svg`.
- ARM runtime dependencies are built by [openlgtv/buildroot-nc4 at 2026.08-webos](https://github.com/openlgtv/buildroot-nc4/tree/2026.08-webos). The build recipe takes libcurl, OpenSSL and their runtime dependencies from the pinned SDK. SDL2, SDL2_image and SDL2_ttf are supplied by the TV firmware. The trust bundle is derived from Mozilla’s CA list through Ubuntu’s ca-certificates package.
- The playback-test pattern and tone are synthesized during packaging with FFmpeg.


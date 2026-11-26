# Privacy

Home, catalog and title screens request metadata and artwork from Cinemeta and configured providers. A search sends its terms to installed add-ons that advertise search support. Choosing a stream or subtitle contacts its provider.

Local profiles, installed manifest URLs, libraries, playback progress and downloads live in the app’s private data directory. The parent PIN is stored as a salted PBKDF2 hash. Provider URLs can contain account-specific configuration. Keep the data directory private.

Linking Nuvio contacts its official API and stores the session tokens locally. Explicit account sync sends pending linked-profile library and progress edits. Disconnect removes the local session and pending uploads. Imported titles remain in the local profile.

The phone remote starts when opened in Settings. Its QR code grants access on the local network. Disconnecting it or switching profiles stops the server. Watch-party controls send the current title and playback position to Nuvio when a user chooses to host or join. The app does not send the selected stream URL to the room.

Timing diagnostics stay on the TV. The client has no analytics or automatic crash-report uploads.


# Open TV Launcher: UI mockups

Design mockups for an open-source Android TV interface. All screens are 1920×1080, use the Inter font, and contain no third-party logos or brand art.

Figma file (launcher screens and components): https://www.figma.com/design/oPwtpOOq34ivqL0KS6Y2qQ

[Test Mokcups Live](https://html-preview.github.io/?url=https://raw.githubusercontent.com/nirodg/open-tv-launcher/refs/heads/main/prototype/index.html?token=GHSAT0AAAAAAEQSUZ24R7UI3I2LDC76U5RA2X4FPIQ)

## Files

| File | Screens |
|---|---|
| `prototype/index.html` | **Clickable prototype**: all screens connected, one at a time, working controls |
| `tv-launcher.html` | Home, App Library, Settings / Picture, Quick Panel, Foundations |
| `store-tv.html` | On-screen keyboard, Store (Discover, Categories, App details, Library, Settings), TV Guide, Now Playing |
| `renders/launcher/`, `renders/store-tv/` | PNG of every screen |
| `scripts/render.cjs` | Re-renders all PNGs from the HTML |

Each HTML file is self-contained. Open it in a browser to view the screens.

## Clickable prototype

Open `prototype/index.html` in a browser. It is one self-contained file with no dependencies.

- **Remote:** arrow keys move, Enter selects, Esc goes back. H goes Home, M opens the quick panel, + and − change the volume, 0 mutes, PgUp and PgDn change channel. Hold Enter or right-click an app for its options. The on-screen remote on the right does the same.
- **Mouse:** hover to focus, click to select, click a slider to set it.
- **What works:** Home and pinned apps (add, move, remove); All apps with filters; Settings with 9 sections where every option works: Wi-Fi scanning and connecting with a password, Bluetooth pairing, a network speed test, resolution changes that ask before they stick, picture controls that change the screen, a screen saver, a sleep timer and turning off when idle, time zone and clock format, per-app storage and permissions, input names, HDMI-CEC, and factory reset; the quick panel; the power menu (standby and restart); the profile picker; and HDMI inputs. In the Store: Discover with a rotating banner, Categories, app details with install, update and uninstall, the Library (updates, Update all, delete), Store settings, and Search with the on-screen keyboard, your own keyboard or simulated voice. In the TV app: a guide that follows the real time, a live preview, Record and Remind, Channels with favourites, Recordings, Sources, and a player with pause, channel change, subtitles, audio tracks, a channel list and controls that hide on their own.

The app names and TV programmes are made up. Nothing is stored between visits.

## Re-render

Needs Node and Playwright with Chromium.

```bash
node scripts/render.cjs
```

## Notes

- App names in the mockups (Reel, Orbit, Streamline and others) are made up.
- The Store assumes F-Droid-style app sources rather than Google Play. Play only ships on Google-certified devices, and Apple's store doesn't run on Android.
- The store and TV screens are only in HTML for now. Building them in Figma stopped at the Starter plan's MCP call limit.

# Open TV Launcher: UI mockups

Design mockups for an open-source Android TV interface. All screens are 1920×1080, use the Inter font, and contain no third-party logos or brand art.

Figma file (launcher screens and components): https://www.figma.com/design/oPwtpOOq34ivqL0KS6Y2qQ

## Files

| File | Screens |
|---|---|
| `tv-launcher.html` | Home, App Library, Settings / Picture, Quick Panel, Foundations |
| `store-tv.html` | On-screen keyboard, Store (Discover, Categories, App details, Library, Settings), TV Guide, Now Playing |
| `renders/launcher/`, `renders/store-tv/` | PNG of every screen |
| `scripts/render.cjs` | Re-renders all PNGs from the HTML |

Each HTML file is self-contained. Open it in a browser to view the screens.

## Re-render

Needs Node and Playwright with Chromium.

```bash
node scripts/render.cjs
```

## Notes

- App names in the mockups (Reel, Orbit, Streamline and others) are made up.
- The Store assumes F-Droid-style app sources rather than Google Play. Play only ships on Google-certified devices, and Apple's store doesn't run on Android.
- The store and TV screens are only in HTML for now. Building them in Figma stopped at the Starter plan's MCP call limit.

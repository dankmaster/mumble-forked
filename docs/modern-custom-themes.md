# Modern Custom Themes

Modern custom themes are versioned JSON token manifests. They contain data only: no HTML, JavaScript, selectors, URLs, imports, or executable styling.

Themes are loaded from the bundled and profile `ModernThemes` directories. The app ships `catppuccin-nord.mumble-theme.json` as an example and copies it to a new profile theme directory without overwriting user files.

## Install a theme

1. Open **Settings > Appearance > Custom themes > Open folder**.
2. Copy a `*.mumble-theme.json` file into the folder.
3. Reopen Settings and select the theme.

The selected ID remains local to the profile. Existing IDs use the `custom:<id>` form and do not change during the JSON migration.

## Manifest format

```json
{
  "formatVersion": 1,
  "id": "midnight-mint",
  "name": "Midnight Mint",
  "appearance": "dark",
  "palette": {
    "shellBackground": "#10151a",
    "crust": "#0b1015",
    "mantle": "#111820",
    "base": "#151c22",
    "surface0": "#1d2830",
    "surface1": "#263640",
    "surface2": "#384b55",
    "text": "#eef8f4",
    "subtext0": "#c7d8d4",
    "overlay0": "#7f9490",
    "accent": "#58d6b3",
    "accentHover": "#78e2c3",
    "accentSubtle": "#2958d6b3",
    "focusAccent": "#58d6b3",
    "red": "#ef7f8d",
    "green": "#68dca8",
    "yellow": "#f0ca73",
    "peach": "#f0a85f"
  },
  "metrics": { "shellRadius": 16, "innerRadius": 11, "spacing": 12 }
}
```

`formatVersion`, `id`, `name`, and all palette entries are required. `appearance` is `dark` or `light`. Colors use formats accepted by `QColor`; `#AARRGGBB` preserves alpha. Radii are clamped to 0–64 and spacing to 0–48.

Bundled manifests live under `src/mumble/themes/`.

## Legacy CSS compatibility

Older profile-local `.css` token themes continue to load for compatibility, and their existing `custom:<id>` settings remain valid. CSS is never loaded from the application bundle and is no longer distributed. A user JSON manifest overrides a legacy CSS theme with the same ID. Authors should migrate to JSON because legacy CSS import is transitional.

## Sharing and testing

Share the JSON manifest plus screenshots of Settings and an active room. Verify text contrast, selection/focus state, status colors, dialogs, menus, timeline, composer, and both built-in and custom accents.

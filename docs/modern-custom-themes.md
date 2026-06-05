# Modern Custom Themes

Modern custom themes are plain CSS files that define CSS custom properties for
the WebEngine Modern shell. They are the supported user-facing modding surface
for visual customization in this fork.

The theme loader reads two folders:

- bundled app themes beside `mumble.exe`, under `ModernThemes`
- profile/user themes, under the profile `ModernThemes` folder opened from
  Settings

The app ships `catppuccin-nord.css` as an example. It is staged beside
`mumble.exe` during the build/install flow and is copied into the profile
`ModernThemes` folder the first time the folder is opened, without overwriting
an existing user copy.

## Install A Theme

1. Open **Settings > Appearance > Custom themes > Open folder**.
2. Copy a `.css` theme file into the opened folder.
3. Reopen Settings, or switch away from and back to the theme picker.
4. Select the theme in the custom theme grid.
5. Pick a built-in accent or **Custom** accent if the theme supports accents.

The selected theme is stored in the local profile. Themes are not synced through
the server and are not sent to other users.

## Create A Theme

Start by copying the minimal theme below or the bundled
`catppuccin-nord.css` example. Give the file a short lowercase name such as
`midnight-mint.css`.

Theme metadata is optional but recommended:

- `mumble-theme-id` is the stable ID stored in settings
- `mumble-theme-name` is the display name in Settings

## Minimal Theme

```css
/* mumble-theme-id: midnight-mint */
/* mumble-theme-name: Midnight Mint */

:root {
	--shell-bg: #10151a;
	--shell-panel: #151c22;
	--shell-panel-soft: #1d2830;
	--shell-rail: #111820;
	--shell-strip: #0b1015;
	--shell-highlight: #263640;
	--text-strong: #eef8f4;
	--text-main: #c7d8d4;
	--text-muted: #7f9490;
	--accent: #58d6b3;
	--accent-rgb: 88, 214, 179;
	--accent-soft: rgba(88, 214, 179, 0.16);
	--accent-border: rgba(88, 214, 179, 0.42);
	--danger: #ef7f8d;
	--warning: #f0ca73;
	--success: #68dca8;
	--latency-orange: #f0a85f;
}
```

The theme ID is optional; without it the file name is used. The theme name is
also optional; without it the file name is shown.

## Test A Theme

Use this quick checklist before sharing a theme:

- Settings preview updates without reopening the client.
- Room list, chat timeline, composer, dialogs, menus, and update banners remain
  readable.
- `--accent`, `--accent-soft`, and `--accent-border` work on selected rows,
  focused controls, buttons, and links.
- Light themes still have enough contrast in cards and embeds.
- Dark themes do not hide muted text, timestamps, separators, or latency
  colors.
- Custom accent still looks intentional when selected in Settings.

If a theme disappears from the grid, check the file for blocked values such as
`url(...)`, `@import`, JavaScript URLs, or full CSS blocks. The first theme
implementation accepts token declarations only.

## Share A Theme

For a pull request or issue attachment, include:

- the `.css` file
- a screenshot of Settings > Appearance using the theme
- a screenshot of an active chat room with messages, embeds, and the composer
- whether the theme is meant to be dark, light, or high contrast
- any accent IDs listed in `--theme-supported-accents`

Bundled themes live under `src/mumble/modern-shell/themes/` and are staged into
the Windows app payload by the normal build/install flow.

## Token Contract

The first implementation intentionally accepts CSS variables only, not full
free-form CSS. This keeps preview fast and lets C++ derive native/window colors
from the same source while classic Qt Widgets are being removed.

Useful variables:

- `--shell-bg`, `--shell-panel`, `--shell-panel-soft`, `--shell-rail`, `--shell-strip`, `--shell-highlight`
- `--text-strong`, `--text-main`, `--text-muted`, `--text-faint`
- `--accent`, `--accent-rgb`, `--accent-soft`, `--accent-border`, `--on-accent`
- `--theme-supported-accents` plus `--theme-accent-<id>`, `--theme-accent-<id>-rgb`, `--theme-accent-<id>-soft`, and `--theme-accent-<id>-border`
- `--danger`, `--warning`, `--success`, `--latency-orange`
- Layout/styling variables such as `--radius-shell`, `--radius-inner`, `--rail-width`, and density-related tokens

Settings also expose a **Custom** accent. It is not a full theme; C++ emits a
small token overlay for `--theme-accent-custom*`, `--theme-supported-accents`,
and `--body-bg-glow`. That overlay composes with both built-in themes and
custom theme files.

Values containing `url(...)`, `@import`, `javascript:`, `expression(...)`, or
CSS blocks are ignored. Future JS or full-CSS mods should be a separate,
explicitly permissioned layer rather than part of this token theme format.

## What Is Not Supported Yet

The current theme system does not support:

- arbitrary JavaScript
- external network assets
- replacing HTML templates
- loading fonts from the web
- server-distributed themes
- per-server theme packs

Those features would need a separate permission and packaging model. Keep theme
files limited to local visual tokens for now.

## Developer Notes

The Modern shell consumes these tokens directly in CSS and through the
`uiTweaks.themeTokens` preview path. C++ also derives `UiThemeTokens` from the
active Modern theme so remaining native/window surfaces can match the shell
while classic Qt Widgets surfaces are being removed.

Theme-related classic surfaces that remain as migration scaffolding:

- `Themes.cpp`, `ThemeInfo.cpp`, and `themes/*.qss` still back non-modern Qt
  stylesheet loading.
- The classic theme settings fields are retained structurally for compatibility,
  but the Modern build no longer uses them as the active theme source.
- Remaining Qt Widget dialogs and native surfaces still consume `UiThemeTokens`/`QPalette` while the WebEngine shell consumes CSS variables.

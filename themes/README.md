# Drop-in themes

Copy any `.theme` file from this folder to `/data/evoplayer/themes/` on the console (FTP). EVO
Player picks them up at startup and they appear in **Settings → Interface & Storage → THEME**.

No rebuild, no repack.

```
/data/evoplayer/
└── themes/
    ├── sunset.theme
    └── nord.theme
```

Format and the full key list: [`docs/ui/theming.md`](../docs/ui/theming.md).

Anything you leave out inherits from the built-in `MIDNIGHT` theme, so a theme
can be three lines long.

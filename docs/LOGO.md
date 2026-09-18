# Lux9 logo

Web files are the originals. Print files are those same images scaled up.
**Place the print file in the layout and scale it down.** Do not scale the web files up.

| File | Size | Use |
|---|---|---|
| `lux9-mark.png` | 1024×1024 | GitHub, favicon, site |
| `lux9-wordmark.png` | 1280×720 | README, site header |
| `lux9-mark-print.png` | 4096×4096 | Book cover, spine, poster |
| `lux9-wordmark-print.png` | 5120×2880 | Title page, wide print |

The `.png` web files are actually JPEGs. The `-print` files are real PNG.

Colors: field `#121212`, gold `#E6B84C` / `#F5B400`, spark white, type white, kicker gray.

## Rebuild print files from the web originals

From the repo root (macOS):

```sh
sips -s format png -z 4096 4096 docs/lux9-mark.png --out docs/lux9-mark-print.png
sips -s format png -z 2880 5120 docs/lux9-wordmark.png --out docs/lux9-wordmark-print.png
```

`-z` is height then width. A 3–4 inch cover mark at 300 dpi is well inside 4096px.

## How the originals were made

Not drawn in Illustrator or SVG. An image model generated them from these prompts. Regenerating will not be pixel-identical; keep the current PNGs if you like them.

**Mark** (square, 1:1):

> A square programming-language logo icon, flat vector, no photorealism, no 3D, no shadows, no gradients except a subtle two-tone. Near-black charcoal background (#121212). Centered geometric mark: a bold, stencil-like numeral 9, heavy strokes, slightly rounded terminals, color warm gold/amber (#E6B84C). Inside the closed loop of the 9, a small white diamond or four-pointed spark (light / lux). Generous padding so it works as a favicon. No extra ornaments, no Plan 9 rabbit, no globe, no circuit traces, no glow haze. Crisp edges as if SVG. No watermark, no mockup, no app icon shine.

**Wordmark** (16:9):

> A wide programming-language wordmark on a near-black charcoal background (#121212), flat vector, no photorealism. Left: a compact gold (#E6B84C) geometric numeral 9 with a tiny white four-pointed spark in its loop. Right of the mark, the text "Lux9" in a clean humanist sans (like IBM Plex Sans or Inter), white, medium-bold, generous letter-spacing. Small subtitle under the word in muted gray, much smaller: "plan 9 · posix". Lots of empty space, like a GitHub social preview. No mockup frames, no URL, no extra icons.

## What not to do

Do not rebuild this as SVG by tracing or approximating the 9. The hand-drawn SVGs did not match. If you ever need a true vector, redraw the Futura-like 9 in a drawing tool from the print PNG as a reference, do not auto-trace.

Public name is **Lux9**. Language, binary, and `.lux` files stay **lux**.

# Steam store & library assets — what to generate

Exact sizes Steam expects. Everything is PNG or JPG unless noted. Make them at
the listed pixel size (Steam is strict). Grouped by "required to publish" vs the
rest.

## Store page — REQUIRED

| Asset | Size (px) | Notes |
|---|---|---|
| **Header capsule** | 460 × 215 | The image on the store page + search results. Most-seen art. Include the logo/title. |
| **Small capsule** | 231 × 87 | Search lists, recommendations. Title must be legible tiny. |
| **Main capsule** | 1232 × 706 | Top of the store page + featured spots. |
| **Vertical capsule** | 748 × 896 | "Coming soon" / sales carousels. |
| **Screenshots** | 1920 × 1080 | **At least 5**, ideally 8–10. Real gameplay, 16:9. |
| **Trailer** | 1920 × 1080 | H.264 .mp4, ~30–90s, 30/60fps. One is enough to launch. |

## Library assets (shown once someone owns it) — do these too

| Asset | Size (px) | Notes |
|---|---|---|
| **Library capsule** | 600 × 900 | Vertical box art in the user's library grid. |
| **Library header** | 920 × 430 | Header in the library detail view. |
| **Library hero** | 3840 × 1240 | Wide banner behind the library page. Keep focal content centered. |
| **Library logo** | 1280 × 720 | **Transparent PNG** — the title logo, composited over the hero. |
| **Client icon** | 32 × 32 | `.ico`/`.tga` — the little icon by the game name in the friends/downloads list. |
| **Community icon** | 184 × 184 | Community hub / badges. |

## Optional but nice

| Asset | Size (px) | Notes |
|---|---|---|
| **Page background** | 1438 × 810 | Full-page store background (often auto-derived from a screenshot). |
| **Bundle / franchise art** | varies | Only if you make a bundle later. |

## Text you'll also need (not images)

- **Short description** (~1–2 sentences, the blurb under the header).
- **Full description** (features, with a few inline images/GIFs).
- **~5–10 tags** (Strategy, Turn-Based, Hex, Fantasy, Singleplayer, …).
- **System requirements** — pull from the real build: 64-bit Windows 10+,
  OpenGL 3.3-capable GPU, ~a few hundred MB disk. (Windows-only today.)
- **AI-content disclosure** — you decided to disclose: tick Steam's AI
  questionnaire and add a line like *"Some 2D art was generated with AI tools
  (Google Gemini/Imagen) and edited by the developer."* Put the same line in the
  in-game credits.

## Tips for generating them

- Screenshots: run the game at 1920×1080 and use the OS/Steam capture — no HUD-
  over-map bugs in frame (fix those first; they're on our list).
- The header/capsules share art but are **different crops** — design a master
  key-art piece, then crop to each aspect ratio (the sizes above are ~2.14:1,
  2.65:1, 1.74:1, 0.83:1 …). Don't just stretch one image.
- Keep the **logo readable at the small-capsule size** — that's the failure mode
  reviewers flag most.

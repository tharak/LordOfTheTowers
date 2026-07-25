# Watabou Generator Bundles — NOTICE

This directory contains pinned, locally patched snapshots of seven procedural
generators by **watabou** (https://watabou.itch.io · https://watabou.github.io ·
Reddit u/watawatabou). They are the "Procgen Arcana" web generators, compiled by
their author from Haxe to JavaScript, embedded here so Watabou Bridge can run
them locally — and offline — inside the Unreal editor against a stable,
known-good data format.

**All rights to the generator code and its assets remain with watabou.** These
files are third-party content and are NOT covered by the license of the
surrounding plugin code. Do not extract these bundles and redistribute them
separately, and do not redistribute them as part of a paid product.

## Redistribution permission

watabou granted explicit permission to bundle these generators with Watabou
Bridge, conditional on the plugin being distributed for free:

> "Some people incorrectly assume that since generators are free to use, they
> can do whatever they want with them (like distributing or even selling them).
> It's not true. But I think in this case it's OK to bundle a few generators
> with your plugin as long as it's distributed for free."
>
> — u/watawatabou, 2026-06-12, in r/FantasyCities:
> https://www.reddit.com/r/FantasyCities/comments/1tyq0g6/comment/or8m9bi/

Accordingly, **Watabou Bridge is distributed free of charge**.

## Pinned versions

| Generator         | Directory            | Bundle             | Version | Upstream                                     |
|-------------------|----------------------|--------------------|---------|----------------------------------------------|
| City Generator    | `city-generator/`    | `mfcg.js`          | 0.11.5  | https://watabou.github.io/city-generator     |
| Dwellings         | `dwellings/`         | `Dwellings.js`     | 1.4.2   | https://watabou.github.io/dwellings          |
| Neighbourhood     | `neighbourhood/`     | `Neighbourhood.js` | 1.2.2   | https://watabou.github.io/neighbourhood      |
| One Page Dungeon  | `one-page-dungeon/`  | `Dungeon.js`       | 1.2.7   | https://watabou.github.io/one-page-dungeon   |
| Perilous Shores   | `perilous-shores/`   | `Perilous.js`      | 1.8.0   | https://watabou.github.io/perilous-shores    |
| Urban Places      | `urban-places/`      | `Urban.js`         | 1.2.1   | https://watabou.github.io/urban-places       |
| Village Generator | `village-generator/` | `Village.js`       | 1.6.6   | https://watabou.github.io/village-generator  |

## Local modifications

The snapshots match their upstream builds except for the patches below — the
same patches the plugin re-applies automatically when a newer bundle is
downloaded via "Update Generators":

1. **`window.__hx` export hook (every bundle):** the minified Haxe class
   registry is additionally exposed as `window.__hx`, so the editor shim can
   call each generator's own JSON exporter directly. No generator logic is
   altered.
2. **Analytics removed (every `index.html`):** Google tag scripts are stripped
   so nothing phones home from the editor's embedded browser.
3. **Dwellings — `Specs.MAX_SIZE` raised (11 → 64):** lets plans encoded from
   large City / Neighbourhood building footprints keep their intended size
   instead of being replaced by a random small plan.
4. **Dwellings — `edge2data` endpoint emit:** door/window records additionally
   carry their wall segment's two endpoints (`va`/`vb`) so the importer can
   place openings on the correct wall.

## Fonts

The fonts shipped inside the bundles (Share Tech, Share Tech Mono, Cinzel,
IM Fell Great Primer) are licensed by their respective authors under the
SIL Open Font License 1.1 and are redistributed here as part of the upstream
bundles.

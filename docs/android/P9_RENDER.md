# P9 render: "the AO setting changes the mipmap level"

Android P9, lane `p9render` (`docs/runtime_port/NEXT_SESSION.md` §2.8). The report: on the Galaxy S25 (Adreno 830,
Vulkan), adjusting the AO quality or strength in the Enhanced tab visibly changes texture mip selection in the
main scene. Branch base `14415217e`. Device: Galaxy S25, release apk 1.0.1 installed 2026-10-25 20:34 (built from
the code of this base). Windows bed: RTX 5080, `BANISHED_RENDER_BACKEND=vulkan` debug flavour of this branch, and
the DX11 exe of `main` as the reference.

**Verdict: NOT REPRODUCED as a sampling defect.** With AO on, off, at strength 0 or 30, toggled at boot or live
in the Options dialog, at render scale Full or Half, the main pass samples every texture exactly as before: the
textured surfaces AO does not reach are pixel-identical. What AO changes is what ASSAO is meant to change, the
same on Vulkan and DX11. The likely source of the impression is in §3. No code was changed.

## 1. What could have caused it, read in the code

Each suspect from the report, and why it does not hold:

- **The AO pass's samplers leaking into the main pass's.** The AO pass binds its own pipeline layout and its own
  descriptor sets (separate SAMPLED_IMAGE / SAMPLER bindings, `ConvertOptions::combineSamplers` off). Its
  samplers live in the backend's one sampler cache (`GetSampler`, `resources.cpp`). After the pass,
  `DrawAoEnd` calls `ResetCommandState`, which drops the cached pipeline, sets, pushed pixel set and vertex
  set. The next game draw pushes its pixel set again from `g_bound.textures`, through `TextureSampler`, which
  takes the bound image's own `sampler`. Nothing in the AO pass writes `g_bound` or an image's sampler.
- **A sampler cache keyed without anisotropy, LOD bias or mip settings.** The key is the whole `SamplerDesc`:
  min / mag / mip filter, compare, three address modes, anisotropy and `maxLod`. It is hashed and compared as
  12 bytes with no padding (`static_assert`). Every other field of `VkSamplerCreateInfo` is a constant in
  `GetSampler`: `mipLodBias` 0, `minLod` 0, the border colour, unnormalized coordinates off. The compare op (LESS) and the
  max anisotropy (clamped to the device limit) follow key fields. Two
  equal keys therefore make equal samplers, and a shared `VkSampler` is immutable. The AO pass's four keys
  (point / linear, clamp / mirror, `maxLod` = `VK_LOD_CLAMP_NONE`) differ from every game texture's key, whose
  `maxLod` is its mip count - 1 or 0. They would be harmless even if they matched.
- **The texture_quality Half path.** The drop is chosen once at boot (`Ext::TextureQuality::Boot`, then
  `SetDrop`). `DropFor` only acts in `ImageBufferData::Create` for a seeded texture, together with its
  samplers' `maxLod`. No AO path creates or re-creates a texture. On the owner's phone the row is off
  (`ext.texture_quality=0`).
- **A settings change re-creating textures.** An AO Apply raises no settings change. `Ext::AmbientOcclusion::Set`
  records the keys. Turning the row on or off only requests the readable main depth (`DrawRequestReadableDepth`),
  which re-creates the backend's own main depth and colour at the next frame. The Options Apply checked on the
  phone wrote no video setting (no `[Vulkan] resize` / `multisample` line). Textures keep their images and the
  samplers they were made with at load (as on DX11).

## 2. What was measured

Seeded new game (`BANISHED_SEED=12345`), camera pinned on the town (`BANISHED_CAMERA_PIN=town,25`). One
screenshot per run, about 50 s after the start, on the phone with `adb exec-out screencap`. On Windows,
`BANISHED_SCREENSHOT_AT=900`. The owner's phone settings were used: MSAA 2x on the tiler path, texture filter 8x,
render scale 100. Only the AO keys (and the render scale for the Half pair) were edited, and settings.cfg was
put back afterwards.

The pairs were not pixel-diffed for a verdict. Instead, the per-pixel luminance ratio (AO on / AO off) was
looked at as an image, and its median taken over fixed boxes:

- If any main-pass sampler, mip range or LOD changed, every textured pixel would move (the ratio image would
  show the textures).
- If only AO acts, the ratio image shows a shading term that follows the geometry, and is exactly 1 elsewhere.

| pair | where | roofs (median) | open grass (median) | water | ratio image |
|---|---|---|---|---|---|
| boot AO High strength 30 vs AO off | S25, Full, MSAA 2x | 1.000 / 0.959 (ridge) | 1.000 | 1.000 | contact shadows at walls, eaves, under and inside tree canopies; textures absent |
| boot AO on, strength 0 vs AO off | S25 | 1.000 | 1.000 | 1.000 | flat: the readable depth alone changes nothing |
| boot AO High 30 vs off, render scale Half | S25, 1170x540 | 1.000 | 1.000 | 1.000 | same as Full, aligned with the geometry |
| live: AO Off -> High in Options, in game | S25 | 0.991 | 1.000 | 1.000 | same as at boot (`main targets ... depth readable` rebuilt at the next frame, no other line) |
| live: strength 20 -> 30 in Options | S25 | 1.000 | 1.000 | 1.000 | stronger AO only |
| boot AO High 30 vs off | Windows Vulkan, MSAA 4x | 0.992 | - | - | geometric AO only |
| boot AO High 30 vs off | Windows DX11 (`main`) | 0.990 | - | - | the same picture as Vulkan |

Away from the AO's reach, the textured surfaces are bit-for-bit the same between AO on and off. A changed
sampler (anisotropy, mip filter, `maxLod`) or a changed image view would change all of them. The Vulkan and DX11
ratio images on Windows match each other feature for feature.

## 3. What the owner most likely saw

- **AO inside the tree canopies.** ASSAO reads the depth of the alpha-tested leaf and needle cards. A canopy is a
  stack of such cards, so ASSAO darkens a canopy heavily and unevenly: in the trees box, the 10th-percentile
  ratio is 0.70-0.78 at strength 30. Strength scales this and quality changes its pattern. Seen at phone size,
  a canopy turns blotchier and looks less detailed, as if a lower mip were sampled. DX11 does the same (same
  ASSAO, same lists: the pass runs after the opaque list, which holds the trees).
- **The season.** A new game starts in early spring. Within the minutes it took to open Options and apply, the
  deciduous trees leafed out on screen (birches bare in one capture, leafy in the next one from the same live
  test). A look at the scene before and after an AO change in the first game minutes catches this.

## 4. Open, and the ruling it needs

- **Ruling:** is AO on the tree canopies wanted? Options:
  - keep it as is (it matches DX11);
  - fade AO on foliage. That would need a foliage mask: ASSAO has no material input, so this would be an
    `ext.ambient_occlusion` change on both backends, not a Vulkan fix.
- Not done: a RenderDoc capture on the phone. The descriptor-level question is already answered by the
  bit-identical surfaces, and RenderDoc on Android still needs the GUI driven by hand (`P8_VULKAN.md` §7.4).
- If the owner sees it again, a screen recording of the moment, with the pinned camera and the same frame
  before and after the Apply, would separate the two §3 causes from anything new.

Evidence files (session scratchpad, not in the tree): the phone and Windows screenshots and ratio images
(`phone_ao_on/off/on0`, `phone_half_on/off`, `phone_live1`, `live1_after`, `win_vk_on/off`, `win_dx_on/off`,
`ratio_*.png`).

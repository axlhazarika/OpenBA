# PROVIDERTABLE hand-in

Not a feature: a hidden limit in `src/ext/resources/resources.cpp`, the table of resource providers. No extension toggle, no save change, no behaviour change while there are 8 providers or fewer.

## 1. What a provider is

`Ext::Resources::Register(prefix, fn, user)` adds one entry to the table that the engine seam (`RtObject::g_extProvideSeam`) consults when the game asks for a resource path the exe does not carry. An entry is a path prefix and a function that builds the object. On `main` there are 7:

| kind | entries |
|---|---|
| text pages (one per embedded `.rsc`) | `Dialog/Extensions`, `Dialog/EnhancedOptions`, `Dialog/ExtensionsWindow`, `Dialog/RestrictedFoodsPage` |
| fonts | `Font/SharpFont`, `Build/SharpFont` |
| sprite sheet | `Dialog/ExtensionsSprites` |

An extension is not a provider. Most are registry rows and engine seams and add none; only one that ships its own UI file (a page, a window, sprites) adds one.

## 2. The defect

The table was `Entry g_table[8]`, so one more provider than `main` has fits and the one after that does not. A ninth `Register()` returned false, and the callers do not act on it. The sprite sheet registers last, so it is the entry that is refused: the log says `[ExtRes] sprites: provider table full, Dialog/ExtensionsSprites not registered`, and every Extensions icon (the toolbar button and each row of the Extensions menu) draws blank. The extension PR open now that adds a page of its own (ext.item_priority) takes the eighth slot, so the next extension that adds a page, a window or a sprite sheet does not fit.

## 3. The change

The table is a `std::vector<Entry>`. `Register()` appends; `Provide()` copies the entry before it calls the provider's function, because a provider that registered another would reallocate the vector. It walks the entries there are when it starts, so one registered during the walk is first seen by the next miss. The vector is never destroyed (the seam points at `Provide()`, and a loader thread can still call it after `main` returns; the old array had no destructor). `Register()` of an identical (prefix, function, user) a second time changes nothing, because the cap no longer makes a repeat visible. `HasProvider()` and `Provide()` loop over the vector. The prefixes are string literals, so the stored pointers stay valid. The comment in `Resources.h` that said `Register` fails when the table is full (8 entries) now says it does not.

Registration happens once at boot and a lookup runs once per missed resource path, so the cost is the same short loop over the same few entries. The faithful profile registers nothing and pays nothing.

## 4. Verified

* Built on Windows (MinGW, release): unit tests pass, `tools/abi_audit.py` OK, `tools/acceptance/acceptance.sh` 0 lines off target, `OpenBA.exe` builds.
* In a running game with the table past 8 entries (two throwaway providers registered ahead of the sprite sheet, not part of this change): the Extensions icons draw. Without the change the sprite sheet is the entry refused and every icon draws blank (seen on a branch with a sixth page).
* No unit test: `resources.cpp` needs the engine's object factory and is not part of the unit build, and the table is not new logic.
* Not verified: Linux and macOS.

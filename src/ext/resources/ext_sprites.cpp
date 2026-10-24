// ============================================================================
// src/ext/resources/ext_sprites.cpp -- the Extensions sprite sheet provider
// (docs/extensions/KITFREE.md §3, block RSCSPRITE). NOT RETAIL.
// ============================================================================
// Provide() asks for one path; the sheet is one build of three objects. The
// asked object is returned (the seam registers it), the two it points at are
// published here first, so a later lookup of their paths finds them. A build is
// repeatable: nothing is cached, so a provider re-run after
// ExternalFactory::Clean makes fresh objects and leaks none (the factory owns
// what it was given; a build that fails destroys what it made).
// ============================================================================

#include "ext/resources/ext_sprites.h"

#include <cstdio>
#include <cstring>

#include "ext/resources/Resources.h"
#include "ext/resources/ext_sprites_data.h"
#include "ext/resources/sheet_builder.h"
#include "graphics/Material.h"
#include "io/Stream.h"
#include "object/External.h"
#include "system/Crc.h"
#include "ui/SpriteSheet.h"
#include "video/ImageBuffer.h"

namespace Ext {
namespace Resources {

namespace {

constexpr const char* kPrefix       = "Dialog/ExtensionsSprites";
constexpr const char* kSheetPath    = "Dialog/ExtensionsSprites.rsc:resource";
constexpr const char* kMaterialPath = "Dialog/ExtensionsSpritesMaterial.rsc:resource";
constexpr const char* kTexturePath  = "Dialog/ExtensionsSpritesTexture.rsc:resource";
constexpr const char* kBaseMaterial = "Material/UI/uiMaterial.rsc";

SheetSpec Spec() {
    SheetSpec s;
    s.sheetWidth      = SpritesData::kSheetWidth;
    s.sheetHeight     = SpritesData::kSheetHeight;
    s.padForFiltering = SpritesData::kPadForFiltering;
    s.materialName    = SpritesData::kMaterialName;
    s.imageName       = SpritesData::kImageName;
    return s;
}

// The cross-check's other half (KITFREE.md §5): the sheet and the material
// written by the REAL IO::BinaryWriter (their Serialize is a pure read on the
// write path; the texture's is not -- it recreates the device texture -- so it
// is not written), logged as byte count + CRC. tests/unit/ext_sheet_builder_test.cpp
// prints the same CRC for the same object written in its mirrored format and
// compares that to the kit's bytes; equal CRCs close the loop. The type ids
// are the kit's header values to compare by eye.
void LogWriterCrc(const SheetObjects& o) {
    struct W {
        static void One(const char* what, Object::External* obj) {
            IO::MemoryStream ms;
            if (!ms.Create(1 << 20)) return;
            obj->Serialize(ms);
            const uint32_t crc = System::CRC::Compute(ms._data, static_cast<int>(ms._cursor), 0xffffffffu);
            std::fprintf(stderr, "[ExtRes] sprites: %s record %u bytes crc %08x (IO::BinaryWriter)\n",
                         what, ms._cursor, crc);
        }
    };
    W::One("sheet", o.sheet);
    W::One("material", o.material);
    std::fprintf(stderr, "[ExtRes] sprites: type ids SpriteSheet %08x MaterialInstance %08x ImageBuffer %08x\n",
                 UI::SpriteSheet::TypeId(), Graphics::MaterialInstance::TypeId(), Video::ImageBuffer::TypeId());
}

Object::External* ProvideSprites(const char* path, void* /*user*/) {
    enum { kSheet, kMaterial, kTexture } asked;
    if (std::strcmp(path, kSheetPath) == 0)         asked = kSheet;
    else if (std::strcmp(path, kMaterialPath) == 0) asked = kMaterial;
    else if (std::strcmp(path, kTexturePath) == 0)  asked = kTexture;
    else {
        std::fprintf(stderr, "[ExtRes] sprites: not ours: %s\n", path);
        return nullptr;
    }

    SheetObjects o = BuildSheetObjects(Spec(), SpritesData::kBitmaps, SpritesData::kBitmapCount,
                                       kBaseMaterial);
    if (o.sheet == nullptr) {
        std::fprintf(stderr, "[ExtRes] sprites: build failed for %s\n", path);
        return nullptr;
    }
    SetPath(o.sheet, kSheetPath);
    SetPath(o.material, kMaterialPath);
    SetPath(o.texture, kTexturePath);

    Object::External* result = nullptr;
    switch (asked) {
    case kSheet:
        Publish(o.texture);
        Publish(o.material);
        result = o.sheet;
        break;
    case kMaterial:
        Publish(o.texture);
        result = o.material;
        delete o.sheet;
        break;
    case kTexture:
        result = o.texture;
        delete o.sheet;
        delete o.material;
        break;
    }
    std::fprintf(stderr, "[ExtRes] sprites: built %s (%d sprites, %d x %d, pad %d) for %s\n",
                 asked == kSheet ? "sheet + material + texture"
                 : asked == kMaterial ? "material + texture" : "texture",
                 SpritesData::kBitmapCount, SpritesData::kSheetWidth, SpritesData::kSheetHeight,
                 SpritesData::kPadForFiltering ? 1 : 0, path);
    if (asked == kSheet) LogWriterCrc(o);
    return result;
}

}  // namespace

void InitSprites() {
    if (!Register(kPrefix, &ProvideSprites, nullptr))
        std::fprintf(stderr, "[ExtRes] sprites: null prefix or function, %s not registered\n", kPrefix);
}

}  // namespace Resources
}  // namespace Ext

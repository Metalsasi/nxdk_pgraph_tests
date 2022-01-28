#include "volume_texture_tests.h"

#include <SDL.h>
#include <pbkit/pbkit.h>

#include <memory>
#include <utility>

#include "debug_output.h"
#include "shaders/perspective_vertex_shader.h"
#include "test_host.h"
#include "texture_format.h"
#include "vertex_buffer.h"

static int GenerateSurface(SDL_Surface **gradient_surface, int width, int height, int layer);
static int GeneratePalettizedSurface(uint8_t **ret, uint32_t width, uint32_t height, uint32_t depth,
                                     TestHost::PaletteSize palette_size);
static uint32_t *GeneratePalette(TestHost::PaletteSize size);

static constexpr uint32_t kBackgroundColor = 0xFE202020;
static const uint32_t kTextureDepth = 4;

VolumeTextureTests::VolumeTextureTests(TestHost &host, std::string output_dir)
    : TestSuite(host, std::move(output_dir), "Volume texture") {
  for (auto i = 0; i < kNumFormats; ++i) {
    auto &format = kTextureFormats[i];
    if (!format.xbox_swizzled) {
      // Linear volumetric formats are not supported by the hardware.
      continue;
    }

    if (format.xbox_format != NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8) {
      tests_[format.name] = [this, format]() { Test(format); };
    }
  }

  auto palettized = GetTextureFormatInfo(NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8);
  tests_[palettized.name] = [this]() { TestPalettized(); };
}

void VolumeTextureTests::Initialize() {
  TestSuite::Initialize();
  CreateGeometry();

  host_.SetShaderProgram(nullptr);
  CreateGeometry();
  host_.SetXDKDefaultViewportAndFixedFunctionMatrices();
  host_.SetTextureStageEnabled(0, true);

  host_.SetShaderStageProgram(TestHost::STAGE_3D_PROJECTIVE);

  host_.SetInputColorCombiner(0, TestHost::SRC_TEX0, false, TestHost::MAP_UNSIGNED_IDENTITY, TestHost::SRC_ZERO, false,
                              TestHost::MAP_UNSIGNED_INVERT);
  host_.SetInputAlphaCombiner(0, TestHost::SRC_TEX0, true, TestHost::MAP_UNSIGNED_IDENTITY, TestHost::SRC_ZERO, false,
                              TestHost::MAP_UNSIGNED_INVERT);

  host_.SetOutputColorCombiner(0, TestHost::DST_DIFFUSE);
  host_.SetOutputAlphaCombiner(0, TestHost::DST_DIFFUSE);

  host_.SetFinalCombiner0(TestHost::SRC_ZERO, false, false, TestHost::SRC_ZERO, false, false, TestHost::SRC_ZERO, false,
                          false, TestHost::SRC_DIFFUSE);
  host_.SetFinalCombiner1(TestHost::SRC_ZERO, false, false, TestHost::SRC_ZERO, false, false, TestHost::SRC_DIFFUSE,
                          true);
}

void VolumeTextureTests::CreateGeometry() {
  const float left = -2.75f;
  const float right = 2.75f;
  const float top = 1.75f;
  const float bottom = -1.75f;
  const float mid_width = 0;
  const float mid_height = 0;

  const uint32_t num_quads = 4;
  std::shared_ptr<VertexBuffer> buffer = host_.AllocateVertexBuffer(6 * num_quads);
  buffer->SetTexCoord0Count(3);

  const float spacing = 0.05f;
  int index = 0;

  buffer->DefineBiTri(index++, left, top, mid_width - spacing, mid_height + spacing);

  buffer->DefineBiTri(index++, mid_width + spacing, top, right, mid_height + spacing);

  buffer->DefineBiTri(index++, left, mid_height - spacing, mid_width - spacing, bottom);

  buffer->DefineBiTri(index++, mid_width + spacing, mid_height - spacing, right, bottom);

  // Set texcoords.
  auto vertex = buffer->Lock();

  auto set_bitri_texcoords = [&vertex](float p) {
    vertex++->SetTexCoord0(0.0f, 0.0f, p, 0.0);
    vertex++->SetTexCoord0(1.0f, 0.0f, p, 0.0);
    vertex++->SetTexCoord0(1.0f, 1.0f, p, 0.0);

    vertex++->SetTexCoord0(0.0f, 0.0f, p, 0.0);
    vertex++->SetTexCoord0(1.0f, 1.0f, p, 0.0);
    vertex++->SetTexCoord0(0.0f, 1.0f, p, 0.0);
  };

  set_bitri_texcoords(0.0f);

  set_bitri_texcoords(0.33f);

  set_bitri_texcoords(0.66f);

  set_bitri_texcoords(1.0f);

  buffer->Unlock();

  buffer->Linearize(static_cast<float>(host_.GetMaxTextureWidth()), static_cast<float>(host_.GetMaxTextureHeight()));
}

void VolumeTextureTests::Test(const TextureFormatInfo &texture_format) {
  host_.SetTextureFormat(texture_format);

  const uint32_t width = host_.GetMaxTextureWidth();
  const uint32_t height = host_.GetMaxTextureHeight();

  auto **layers = new SDL_Surface *[kTextureDepth];

  for (auto d = 0; d < kTextureDepth; ++d) {
    int update_texture_result = GenerateSurface(&layers[d], (int)width, (int)height, d);
    ASSERT(!update_texture_result && "Failed to generate SDL surface");
  }

  int update_texture_result = host_.SetVolumetricTexture((const SDL_Surface **)layers, kTextureDepth);
  ASSERT(!update_texture_result && "Failed to set texture");

  for (auto d = 0; d < kTextureDepth; ++d) {
    SDL_FreeSurface(layers[d]);
  }
  delete[] layers;

  auto &stage = host_.GetTextureStage(0);
  stage.SetDimensions(width, height, kTextureDepth);

  host_.PrepareDraw(kBackgroundColor);
  host_.DrawArrays();

  pb_print("N: %s\n", texture_format.name);
  pb_print("F: 0x%x\n", texture_format.xbox_format);
  pb_print("SZ: %d\n", texture_format.xbox_swizzled);
  pb_print("C: %d\n", texture_format.require_conversion);
  pb_draw_text_screen();

  host_.FinishDraw(allow_saving_, output_dir_, texture_format.name);
}

void VolumeTextureTests::TestPalettized() {
  host_.PrepareDraw(kBackgroundColor);

  TestHost::PaletteSize palette_size = TestHost::PALETTE_256;
  auto &texture_format = GetTextureFormatInfo(NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8);
  host_.SetTextureFormat(texture_format);

  uint8_t *surface = nullptr;
  const uint32_t width = host_.GetMaxTextureWidth();
  const uint32_t height = host_.GetMaxTextureHeight();
  int err = GeneratePalettizedSurface(&surface, width, height, kTextureDepth, palette_size);
  ASSERT(!err && "Failed to generate palettized surface");

  err = host_.SetRawTexture(surface, width, height, kTextureDepth, width, 1, texture_format.xbox_swizzled);
  delete[] surface;
  ASSERT(!err && "Failed to set texture");

  auto stage = host_.GetTextureStage(0);
  stage.SetDimensions(width, height, kTextureDepth);

  auto palette = GeneratePalette(palette_size);
  err = host_.SetPalette(palette, palette_size);
  delete[] palette;
  ASSERT(!err && "Failed to set palette");

  host_.DrawArrays();

  pb_print("N: %s\n", texture_format.name);
  pb_print("F: 0x%x\n", texture_format.xbox_format);
  pb_print("SZ: %d\n", texture_format.xbox_swizzled);
  pb_print("C: %d\n", texture_format.require_conversion);
  pb_draw_text_screen();

  host_.FinishDraw(allow_saving_, output_dir_, texture_format.name);
}

static int GenerateSurface(SDL_Surface **gradient_surface, int width, int height, int layer) {
  *gradient_surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA8888);
  if (!(*gradient_surface)) {
    return 1;
  }

  if (SDL_LockSurface(*gradient_surface)) {
    SDL_FreeSurface(*gradient_surface);
    *gradient_surface = nullptr;
    return 2;
  }

  uint32_t red_mask = 0xFF;
  uint32_t green_mask = 0xFF;
  uint32_t blue_mask = 0xFF;

  layer %= 4;

  if (layer == 1) {
    red_mask = 0;
    green_mask = 0;
  } else if (layer == 2) {
    green_mask = 0;
    blue_mask = 0;
  } else if (layer == 3) {
    red_mask = 0;
    blue_mask = 0;
  }

  auto pixels = static_cast<uint32_t *>((*gradient_surface)->pixels);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x, ++pixels) {
      int x_normal = static_cast<int>(static_cast<float>(x) * 255.0f / static_cast<float>(width));
      int y_normal = static_cast<int>(static_cast<float>(y) * 255.0f / static_cast<float>(height));
      *pixels = SDL_MapRGBA((*gradient_surface)->format, y_normal & red_mask, x_normal & green_mask,
                            (255 - y_normal) & blue_mask, x_normal + y_normal);
    }
  }

  SDL_UnlockSurface(*gradient_surface);

  return 0;
}

static int GeneratePalettizedSurface(uint8_t **ret, uint32_t width, uint32_t height, uint32_t depth,
                                     TestHost::PaletteSize palette_size) {
  *ret = new uint8_t[width * height * depth];
  if (!(*ret)) {
    return 1;
  }

  auto pixel = *ret;

  for (auto d = 0; d < depth; ++d) {
    uint32_t layer_size = width * height;

    uint32_t half_size = layer_size >> 1;

    for (uint32_t i = 0; i < half_size; ++i, ++pixel) {
      *pixel = (d << 2) & (palette_size - 1);
    }

    for (uint32_t i = half_size; i < layer_size; i += 4) {
      uint8_t value = (i + (d << 2)) & (palette_size - 1);
      *pixel++ = value;
      *pixel++ = value;
      *pixel++ = value;
      *pixel++ = value;
    }
  }

  return 0;
}

static uint32_t *GeneratePalette(TestHost::PaletteSize size) {
  auto ret = new uint32_t[size];

  uint32_t block_size = size / 4;
  auto component_inc = (uint32_t)ceilf(255.0f / (float)block_size);
  uint32_t i = 0;
  uint32_t component = 0;
  for (; i < block_size; ++i, component += component_inc) {
    uint32_t color_value = 0xFF - component;
    ret[i + block_size * 0] = 0xFF000000 + color_value;
    ret[i + block_size * 1] = 0xFF000000 + (color_value << 8);
    ret[i + block_size * 2] = 0xFF000000 + (color_value << 16);
    ret[i + block_size * 3] = 0xFF000000 + color_value + (color_value << 8) + (color_value << 16);
  }

  return ret;
}
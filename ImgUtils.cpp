#include "ImgUtils.hpp"
#include <GLES3/gl32.h>
#include <cairo/cairo.h>
#include <filesystem>
#include <format>
#include <hyprland/src/debug/Log.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Texture.hpp>

static SP<CTexture> invalidImageTexture = nullptr;
void initInvalidImageTexture() {
  SP<CTexture> tex = makeShared<CTexture>();
  tex->allocate();

  const auto CAIROSURFACE =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 512, 512);
  const auto CAIRO = cairo_create(CAIROSURFACE);

  cairo_set_antialias(CAIRO, CAIRO_ANTIALIAS_NONE);
  cairo_save(CAIRO);
  cairo_set_source_rgba(CAIRO, 0, 0, 0, 1);
  cairo_set_operator(CAIRO, CAIRO_OPERATOR_SOURCE);
  cairo_paint(CAIRO);
  cairo_set_source_rgba(CAIRO, 1, 0, 1, 1);
  cairo_rectangle(CAIRO, 256, 0, 256, 256);
  cairo_fill(CAIRO);
  cairo_rectangle(CAIRO, 0, 256, 256, 256);
  cairo_fill(CAIRO);
  cairo_restore(CAIRO);

  cairo_surface_flush(CAIROSURFACE);

  tex->m_size = {512, 512};

  // Copy the data to an OpenGL texture we have
  const GLint glFormat = GL_RGBA;
  const GLint glType = GL_UNSIGNED_BYTE;

  const auto DATA = cairo_image_surface_get_data(CAIROSURFACE);
  glBindTexture(GL_TEXTURE_2D, tex->m_texID);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
  glTexImage2D(GL_TEXTURE_2D, 0, glFormat, tex->m_size.x, tex->m_size.y, 0,
               glFormat, glType, DATA);

  cairo_surface_destroy(CAIROSURFACE);
  cairo_destroy(CAIRO);

  invalidImageTexture = tex;
}

SP<CTexture> ImgUtils::load(const std::string &fullPath) {
  if (!std::filesystem::exists(fullPath)) {
    Debug::log(ERR, "ImgUtils failed to load {} (image doesn't exist. typo?)",
               fullPath);
    if (!invalidImageTexture) {
      initInvalidImageTexture();
    }
    return invalidImageTexture;
  }

  const auto CAIROSURFACE =
      cairo_image_surface_create_from_png(fullPath.c_str());

  if (!CAIROSURFACE) {
    Debug::log(ERR,
               "ImgUtils failed to load {} (corrupt / inaccessible / not png)",
               fullPath);
    if (!invalidImageTexture) {
      initInvalidImageTexture();
    }
    return invalidImageTexture;
  }

  const auto CAIROFORMAT = cairo_image_surface_get_format(CAIROSURFACE);
  auto tex = makeShared<CTexture>();

  // Debug: Log the Cairo format to help diagnose transparency issues
  Debug::log(LOG, "ImgUtils loading {}, Cairo format: {}", fullPath, (int)CAIROFORMAT);
  
  tex->allocate();
  tex->m_size = {cairo_image_surface_get_width(CAIROSURFACE),
                 cairo_image_surface_get_height(CAIROSURFACE)};

  // Improved format handling for transparency
  GLint glIFormat, glFormat;
  const GLint glType = CAIROFORMAT == CAIRO_FORMAT_RGB96F ? GL_FLOAT : GL_UNSIGNED_BYTE;
  
  switch (CAIROFORMAT) {
    case CAIRO_FORMAT_ARGB32:
    case CAIRO_FORMAT_A8:
      glIFormat = GL_RGBA;
      glFormat = GL_RGBA;
      Debug::log(LOG, "ImgUtils: Loading with alpha channel support");
      break;
    case CAIRO_FORMAT_RGB24:
      glIFormat = GL_RGB;
      glFormat = GL_RGB;
      Debug::log(WARN, "ImgUtils: Loading RGB image without alpha channel - transparency won't work");
      break;
    case CAIRO_FORMAT_RGB96F:
      glIFormat = GL_RGB32F;
      glFormat = GL_RGB;
      break;
    default:
      Debug::log(WARN, "ImgUtils: Unknown Cairo format {}, defaulting to RGBA", (int)CAIROFORMAT);
      glIFormat = GL_RGBA;
      glFormat = GL_RGBA;
      break;
  }

  const auto DATA = cairo_image_surface_get_data(CAIROSURFACE);
  glBindTexture(GL_TEXTURE_2D, tex->m_texID);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

  // Only apply color swizzling for formats that need it
  if (CAIROFORMAT == CAIRO_FORMAT_ARGB32 || CAIROFORMAT == CAIRO_FORMAT_RGB24) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
  }

  glTexImage2D(GL_TEXTURE_2D, 0, glIFormat, tex->m_size.x, tex->m_size.y, 0,
               glFormat, glType, DATA);

  cairo_surface_destroy(CAIROSURFACE);

  return tex;
}

SP<CTexture> ImgUtils::sliceTexture(SP<CTexture> src, const CBox &box) {
  if (box.width == 0 || box.height == 0) {
    return nullptr;
  }

  // Initialize the dst texture
  // ------------

  SP<CTexture> tex = makeShared<CTexture>();
  tex->allocate();
  tex->m_size = box.size();

  const GLint glIFormat = GL_RGBA;
  const GLint glFormat = GL_RGBA;
  const GLint glType = GL_UNSIGNED_BYTE;
  const int DATA_SIZE = box.width * box.height * 4;
  const auto DATA = new unsigned char[DATA_SIZE]{0};
  glBindTexture(GL_TEXTURE_2D, tex->m_texID);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
  glTexImage2D(GL_TEXTURE_2D, 0, glIFormat, tex->m_size.x, tex->m_size.y, 0,
               glFormat, glType, DATA);
  delete[] DATA;

  // Copy the region from src to dst
  // ------------

  glCopyImageSubData(src->m_texID, GL_TEXTURE_2D, 0, box.x, box.y, 0,
                     tex->m_texID, GL_TEXTURE_2D, 0, 0, 0, 0, tex->m_size.x,
                     tex->m_size.y, 1);

  return tex;
}

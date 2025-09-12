#include "ImgBorder.hpp"
#include "ImgBorderPassElement.hpp"
#include "ImgUtils.hpp"
#include "globals.hpp"
#include <filesystem>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/debug/Log.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/decorations/DecorationPositioner.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <wordexp.h>

CImgBorder::CImgBorder(PHLWINDOW pWindow) : IHyprWindowDecoration(pWindow) {
  m_pWindow = pWindow;
  updateConfig();
}

CImgBorder::~CImgBorder() { std::erase(g_pGlobalState->borders, m_self); }

SDecorationPositioningInfo CImgBorder::getPositioningInfo() {
  SDecorationPositioningInfo info;
  info.policy = DECORATION_POSITION_STICKY;
  info.edges = DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT |
               DECORATION_EDGE_TOP | DECORATION_EDGE_BOTTOM;
  info.priority = 9990;
  if (m_isEnabled && !m_isHidden) {
    info.desiredExtents = {
        .topLeft = {(m_sizes[0] - m_insets[0]) * m_scale,
                    (m_sizes[2] - m_insets[2]) * m_scale},
        .bottomRight = {(m_sizes[1] - m_insets[1]) * m_scale,
                        (m_sizes[3] - m_insets[3]) * m_scale},
    };
  }
  info.reserved = true;
  return info;
}

void CImgBorder::onPositioningReply(const SDecorationPositioningReply &reply) {}

void CImgBorder::draw(PHLMONITOR pMonitor, const float &a) {
  if (!validMapped(m_pWindow))
    return;

  const auto PWINDOW = m_pWindow.lock();

  if (!PWINDOW->m_windowData.decorate.valueOrDefault())
    return;

  CImgBorderPassElement::SData data = {
      .deco = this,
      .a = 1.F,
  };
  g_pHyprRenderer->m_renderPass.add(makeUnique<CImgBorderPassElement>(data));
}

bool CImgBorder::shouldBlur() { return m_shouldBlurGlobal && m_shouldBlur; }

CBox CImgBorder::getGlobalBoundingBox(PHLMONITOR pMonitor) {
  const auto PWINDOW = m_pWindow.lock();

  // idk if I should be doing it this way but it works so...
  CBox box = PWINDOW->getWindowMainSurfaceBox();
  box.width += (m_sizes[0] - m_insets[0]) * m_scale +
               (m_sizes[1] - m_insets[1]) * m_scale;
  box.height += (m_sizes[2] - m_insets[2]) * m_scale +
                (m_sizes[3] - m_insets[3]) * m_scale;
  box.translate(-Vector2D{(m_sizes[0] - m_insets[0]) * m_scale,
                          (m_sizes[2] - m_insets[2]) * m_scale});

  const auto PWORKSPACE = PWINDOW->m_workspace;
  const auto WORKSPACEOFFSET = PWORKSPACE && !m_pWindow->m_pinned
                                   ? PWORKSPACE->m_renderOffset->value()
                                   : Vector2D();
  box.translate(PWINDOW->m_floatingOffset - pMonitor->m_position +
                WORKSPACEOFFSET);

  m_bLastRelativeBox = box;

  return box;
}

void CImgBorder::drawPass(PHLMONITOR pMonitor, const float &a) {
  if (!m_isEnabled || m_isHidden)
    return;

  const auto box = getGlobalBoundingBox(pMonitor);

  // For debugging
  // g_pHyprOpenGL->renderRect(box, CHyprColor{1.0, 0.0, 0.0, 0.5});
  // return;

  // Render the textures
  // ------------

  const auto BORDER_LEFT = (float)m_sizes[0] * m_scale;
  const auto BORDER_RIGHT = (float)m_sizes[1] * m_scale;
  const auto BORDER_TOP = (float)m_sizes[2] * m_scale;
  const auto BORDER_BOTTOM = (float)m_sizes[3] * m_scale;

  const auto HEIGHT_MID = box.height - BORDER_TOP - BORDER_BOTTOM;
  const auto WIDTH_MID = box.width - BORDER_LEFT - BORDER_RIGHT;

  // Save previous values

  const auto wasUsingNearestNeighbour =
      g_pHyprOpenGL->m_renderData.useNearestNeighbor;
  const auto prevDiscardMode = g_pHyprOpenGL->m_renderData.discardMode;
  const auto prevDiscardOpacity = g_pHyprOpenGL->m_renderData.discardOpacity;
  const auto prevUVTL = g_pHyprOpenGL->m_renderData.primarySurfaceUVTopLeft;
  const auto prevUVBR = g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight;

  g_pHyprOpenGL->m_renderData.useNearestNeighbor = !m_shouldSmooth;
  g_pHyprOpenGL->m_renderData.discardMode = DISCARD_ALPHA;
  g_pHyprOpenGL->m_renderData.discardOpacity = 0.01f; // Discard only nearly transparent pixels


  // Enhanced 7-section edges

  g_pHyprOpenGL->m_renderData.primarySurfaceUVTopLeft = {0, 0};
  g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};

  // Calculate section widths and heights for positioning
  const auto HOR_LEFT_LEFT = (float)m_hor_sizes[0] * m_scale;
  const auto HOR_LEFT_RIGHT = (float)m_hor_sizes[1] * m_scale;
  const auto HOR_RIGHT_LEFT = (float)m_hor_sizes[2] * m_scale;
  const auto HOR_RIGHT_RIGHT = (float)m_hor_sizes[3] * m_scale;
  
  const auto VER_TOP_TOP = (float)m_ver_sizes[0] * m_scale;
  const auto VER_TOP_BOT = (float)m_ver_sizes[1] * m_scale;
  const auto VER_BOT_TOP = (float)m_ver_sizes[2] * m_scale;
  const auto VER_BOT_BOT = (float)m_ver_sizes[3] * m_scale;

  // Calculate middle section dimensions
  const auto TOP_MID_WIDTH = WIDTH_MID - HOR_LEFT_LEFT - HOR_LEFT_RIGHT - HOR_RIGHT_LEFT - HOR_RIGHT_RIGHT;
  const auto LEFT_MID_HEIGHT = HEIGHT_MID - VER_TOP_TOP - VER_TOP_BOT - VER_BOT_TOP - VER_BOT_BOT;

  // TOP EDGE (7 sections with placement-based positioning)
  const float top_start_x = box.x + BORDER_LEFT;
  const float top_available_width = WIDTH_MID;
  
  // Calculate custom positions as percentages of available width
  const float tlc_pos = (m_top_placements[0] / 100.0f) * top_available_width;
  const float trc_pos = (m_top_placements[1] / 100.0f) * top_available_width;
  
  // Calculate tiling section widths based on custom positions
  const float tle_width = tlc_pos;
  const float tme_width = trc_pos - tlc_pos - HOR_LEFT_RIGHT;
  const float tre_width = top_available_width - trc_pos - HOR_RIGHT_LEFT;
  
  float top_x = top_start_x;
  
  // Left edge tiling section
  if (m_tex_tle && tle_width > 0) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight.x = tle_width / (m_tex_tle->m_size.x * m_scale);
    const CBox box_tle = {{top_x, box.y}, {tle_width, BORDER_TOP}};
    g_pHyprOpenGL->renderTexture(m_tex_tle, box_tle, {.a = a, .blur = shouldBlur(), .allowCustomUV = true, .wrapX = GL_REPEAT, .wrapY = GL_REPEAT});
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
  }
  
  // Left custom section at placement position
  if (m_tex_tlc) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
    const CBox box_tlc = {{top_start_x + tlc_pos, box.y}, {HOR_LEFT_RIGHT, BORDER_TOP}};
    g_pHyprOpenGL->renderTexture(m_tex_tlc, box_tlc, {.a = a, .blur = shouldBlur()});
  }
  
  // Middle edge tiling section
  if (m_tex_tme && tme_width > 0) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight.x = tme_width / (m_tex_tme->m_size.x * m_scale);
    const CBox box_tme = {{top_start_x + tlc_pos + HOR_LEFT_RIGHT, box.y}, {tme_width, BORDER_TOP}};
    g_pHyprOpenGL->renderTexture(m_tex_tme, box_tme, {.a = a, .blur = shouldBlur(), .allowCustomUV = true, .wrapX = GL_REPEAT, .wrapY = GL_REPEAT});
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
  }
  
  // Right custom section at placement position
  if (m_tex_trc) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
    const CBox box_trc = {{top_start_x + trc_pos, box.y}, {HOR_RIGHT_LEFT, BORDER_TOP}};
    g_pHyprOpenGL->renderTexture(m_tex_trc, box_trc, {.a = a, .blur = shouldBlur()});
  }
  
  // Right edge tiling section
  if (m_tex_tre && tre_width > 0) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight.x = tre_width / (m_tex_tre->m_size.x * m_scale);
    const CBox box_tre = {{top_start_x + trc_pos + HOR_RIGHT_LEFT, box.y}, {tre_width, BORDER_TOP}};
    g_pHyprOpenGL->renderTexture(m_tex_tre, box_tre, {.a = a, .blur = shouldBlur(), .allowCustomUV = true, .wrapX = GL_REPEAT, .wrapY = GL_REPEAT});
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
  }

  // RIGHT EDGE (7 sections with placement-based positioning)
  const float right_start_y = box.y + BORDER_TOP;
  const float right_available_height = HEIGHT_MID;
  
  // Calculate custom positions as percentages of available height
  const float rtc_pos = (m_right_placements[0] / 100.0f) * right_available_height;
  const float rbc_pos = (m_right_placements[1] / 100.0f) * right_available_height;
  
  // Calculate tiling section heights based on custom positions
  const float rte_height = rtc_pos;
  const float rme_height = rbc_pos - rtc_pos - VER_TOP_BOT;
  const float rbe_height = right_available_height - rbc_pos - VER_BOT_TOP;
  
  // Top edge tiling section
  if (m_tex_rte && rte_height > 0) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight.y = rte_height / (m_tex_rte->m_size.y * m_scale);
    const CBox box_rte = {{box.x + box.width - BORDER_RIGHT, right_start_y}, {BORDER_RIGHT, rte_height}};
    g_pHyprOpenGL->renderTexture(m_tex_rte, box_rte, {.a = a, .blur = shouldBlur(), .allowCustomUV = true, .wrapX = GL_REPEAT, .wrapY = GL_REPEAT});
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
  }
  
  // Top custom section at placement position
  if (m_tex_rtc) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
    const CBox box_rtc = {{box.x + box.width - BORDER_RIGHT, right_start_y + rtc_pos}, {BORDER_RIGHT, VER_TOP_BOT}};
    g_pHyprOpenGL->renderTexture(m_tex_rtc, box_rtc, {.a = a, .blur = shouldBlur()});
  }
  
  // Middle edge tiling section
  if (m_tex_rme && rme_height > 0) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight.y = rme_height / (m_tex_rme->m_size.y * m_scale);
    const CBox box_rme = {{box.x + box.width - BORDER_RIGHT, right_start_y + rtc_pos + VER_TOP_BOT}, {BORDER_RIGHT, rme_height}};
    g_pHyprOpenGL->renderTexture(m_tex_rme, box_rme, {.a = a, .blur = shouldBlur(), .allowCustomUV = true, .wrapX = GL_REPEAT, .wrapY = GL_REPEAT});
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
  }
  
  // Bottom custom section at placement position
  if (m_tex_rbc) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
    const CBox box_rbc = {{box.x + box.width - BORDER_RIGHT, right_start_y + rbc_pos}, {BORDER_RIGHT, VER_BOT_TOP}};
    g_pHyprOpenGL->renderTexture(m_tex_rbc, box_rbc, {.a = a, .blur = shouldBlur()});
  }
  
  // Bottom edge tiling section
  if (m_tex_rbe && rbe_height > 0) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight.y = rbe_height / (m_tex_rbe->m_size.y * m_scale);
    const CBox box_rbe = {{box.x + box.width - BORDER_RIGHT, right_start_y + rbc_pos + VER_BOT_TOP}, {BORDER_RIGHT, rbe_height}};
    g_pHyprOpenGL->renderTexture(m_tex_rbe, box_rbe, {.a = a, .blur = shouldBlur(), .allowCustomUV = true, .wrapX = GL_REPEAT, .wrapY = GL_REPEAT});
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
  }

  // BOTTOM EDGE (7 sections with placement-based positioning)
  const float bottom_start_x = box.x + BORDER_LEFT;
  const float bottom_available_width = WIDTH_MID;
  
  // Calculate custom positions as percentages of available width
  const float blc_pos = (m_bottom_placements[0] / 100.0f) * bottom_available_width;
  const float brc_pos = (m_bottom_placements[1] / 100.0f) * bottom_available_width;
  
  // Calculate tiling section widths based on custom positions
  const float ble_width = blc_pos;
  const float bme_width = brc_pos - blc_pos - HOR_LEFT_RIGHT;
  const float bre_width = bottom_available_width - brc_pos - HOR_RIGHT_LEFT;
  
  // Left edge tiling section
  if (m_tex_ble && ble_width > 0) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight.x = ble_width / (m_tex_ble->m_size.x * m_scale);
    const CBox box_ble = {{bottom_start_x, box.y + box.height - BORDER_BOTTOM}, {ble_width, BORDER_BOTTOM}};
    g_pHyprOpenGL->renderTexture(m_tex_ble, box_ble, {.a = a, .blur = shouldBlur(), .allowCustomUV = true, .wrapX = GL_REPEAT, .wrapY = GL_REPEAT});
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
  }
  
  // Left custom section at placement position
  if (m_tex_blc) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
    const CBox box_blc = {{bottom_start_x + blc_pos, box.y + box.height - BORDER_BOTTOM}, {HOR_LEFT_RIGHT, BORDER_BOTTOM}};
    g_pHyprOpenGL->renderTexture(m_tex_blc, box_blc, {.a = a, .blur = shouldBlur()});
  }
  
  // Middle edge tiling section
  if (m_tex_bme && bme_width > 0) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight.x = bme_width / (m_tex_bme->m_size.x * m_scale);
    const CBox box_bme = {{bottom_start_x + blc_pos + HOR_LEFT_RIGHT, box.y + box.height - BORDER_BOTTOM}, {bme_width, BORDER_BOTTOM}};
    g_pHyprOpenGL->renderTexture(m_tex_bme, box_bme, {.a = a, .blur = shouldBlur(), .allowCustomUV = true, .wrapX = GL_REPEAT, .wrapY = GL_REPEAT});
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
  }
  
  // Right custom section at placement position
  if (m_tex_brc) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
    const CBox box_brc = {{bottom_start_x + brc_pos, box.y + box.height - BORDER_BOTTOM}, {HOR_RIGHT_LEFT, BORDER_BOTTOM}};
    g_pHyprOpenGL->renderTexture(m_tex_brc, box_brc, {.a = a, .blur = shouldBlur()});
  }
  
  // Right edge tiling section
  if (m_tex_bre && bre_width > 0) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight.x = bre_width / (m_tex_bre->m_size.x * m_scale);
    const CBox box_bre = {{bottom_start_x + brc_pos + HOR_RIGHT_LEFT, box.y + box.height - BORDER_BOTTOM}, {bre_width, BORDER_BOTTOM}};
    g_pHyprOpenGL->renderTexture(m_tex_bre, box_bre, {.a = a, .blur = shouldBlur(), .allowCustomUV = true, .wrapX = GL_REPEAT, .wrapY = GL_REPEAT});
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
  }

  // LEFT EDGE (7 sections with placement-based positioning)
  const float left_start_y = box.y + BORDER_TOP;
  const float left_available_height = HEIGHT_MID;
  
  // Calculate custom positions as percentages of available height
  const float ltc_pos = (m_left_placements[0] / 100.0f) * left_available_height;
  const float lbc_pos = (m_left_placements[1] / 100.0f) * left_available_height;
  
  // Calculate tiling section heights based on custom positions
  const float lte_height = ltc_pos;
  const float lme_height = lbc_pos - ltc_pos - VER_TOP_BOT;
  const float lbe_height = left_available_height - lbc_pos - VER_BOT_TOP;
  
  // Top edge tiling section
  if (m_tex_lte && lte_height > 0) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight.y = lte_height / (m_tex_lte->m_size.y * m_scale);
    const CBox box_lte = {{box.x, left_start_y}, {BORDER_LEFT, lte_height}};
    g_pHyprOpenGL->renderTexture(m_tex_lte, box_lte, {.a = a, .blur = shouldBlur(), .allowCustomUV = true, .wrapX = GL_REPEAT, .wrapY = GL_REPEAT});
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
  }
  
  // Top custom section at placement position
  if (m_tex_ltc) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
    const CBox box_ltc = {{box.x, left_start_y + ltc_pos}, {BORDER_LEFT, VER_TOP_BOT}};
    g_pHyprOpenGL->renderTexture(m_tex_ltc, box_ltc, {.a = a, .blur = shouldBlur()});
  }
  
  // Middle edge tiling section
  if (m_tex_lme && lme_height > 0) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight.y = lme_height / (m_tex_lme->m_size.y * m_scale);
    const CBox box_lme = {{box.x, left_start_y + ltc_pos + VER_TOP_BOT}, {BORDER_LEFT, lme_height}};
    g_pHyprOpenGL->renderTexture(m_tex_lme, box_lme, {.a = a, .blur = shouldBlur(), .allowCustomUV = true, .wrapX = GL_REPEAT, .wrapY = GL_REPEAT});
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
  }
  
  // Bottom custom section at placement position
  if (m_tex_lbc) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
    const CBox box_lbc = {{box.x, left_start_y + lbc_pos}, {BORDER_LEFT, VER_BOT_TOP}};
    g_pHyprOpenGL->renderTexture(m_tex_lbc, box_lbc, {.a = a, .blur = shouldBlur()});
  }
  
  // Bottom edge tiling section
  if (m_tex_lbe && lbe_height > 0) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight.y = lbe_height / (m_tex_lbe->m_size.y * m_scale);
    const CBox box_lbe = {{box.x, left_start_y + lbc_pos + VER_BOT_TOP}, {BORDER_LEFT, lbe_height}};
    g_pHyprOpenGL->renderTexture(m_tex_lbe, box_lbe, {.a = a, .blur = shouldBlur(), .allowCustomUV = true, .wrapX = GL_REPEAT, .wrapY = GL_REPEAT});
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
  }

  // Corners

  if (m_tex_br) {
    const CBox box_br = {
        {box.x + box.width - BORDER_RIGHT, box.y + box.height - BORDER_BOTTOM},
        {BORDER_RIGHT, BORDER_BOTTOM}};
      g_pHyprOpenGL->renderTexture(m_tex_br, box_br, {.a = a});
  }

  if (m_tex_bl) {
    const CBox box_bl = {{box.x, box.y + box.height - BORDER_BOTTOM},
                         {BORDER_LEFT, BORDER_BOTTOM}};
      g_pHyprOpenGL->renderTexture(m_tex_bl, box_bl, {.a = a});
  }

  if (m_tex_tl) {
    const CBox box_tl = {box.pos(), {BORDER_LEFT, BORDER_TOP}};
      g_pHyprOpenGL->renderTexture(m_tex_tl, box_tl, {.a = a});
  }

  if (m_tex_tr) {
    const CBox box_tr = {{box.x + box.width - BORDER_RIGHT, box.y},
                         {BORDER_RIGHT, BORDER_TOP}};
        g_pHyprOpenGL->renderTexture(m_tex_tr, box_tr, {.a = a});
  }

  // Restore previous values

  g_pHyprOpenGL->m_renderData.useNearestNeighbor = wasUsingNearestNeighbour;
  g_pHyprOpenGL->m_renderData.discardMode = prevDiscardMode;
  g_pHyprOpenGL->m_renderData.discardOpacity = prevDiscardOpacity;

  g_pHyprOpenGL->m_renderData.primarySurfaceUVTopLeft = prevUVTL;
  g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = prevUVBR;
}

eDecorationType CImgBorder::getDecorationType() { return DECORATION_CUSTOM; }

void CImgBorder::updateWindow(PHLWINDOW pWindow) { damageEntire(); }

void CImgBorder::damageEntire() {
  g_pHyprRenderer->damageBox(m_bLastRelativeBox.expand(2));
}

eDecorationLayer CImgBorder::getDecorationLayer() {
  return DECORATION_LAYER_OVER;
}

uint64_t CImgBorder::getDecorationFlags() {
  return DECORATION_PART_OF_MAIN_WINDOW;
}

template<int N>
bool parseInts(Hyprlang::STRING const *str, int (&outArr)[N]) {
  auto strStream = std::stringstream(*str);
  for (int i = 0; i < N; i++) {
    try {
      std::string intStr;
      std::getline(strStream, intStr, ',');
      outArr[i] = std::stoi(intStr);
    } catch (...) {
      return false;
    }
  }
  return true;
}

// TODO better error handling
void CImgBorder::updateConfig() {
  // Read config
  // ------------

  // hidden
  m_isEnabled = **(Hyprlang::INT *const *)HyprlandAPI::getConfigValue(
                      PHANDLE, "plugin:imgborders:enabled")
                      ->getDataStaticPtr();
  if (!m_isEnabled) {
    return;
  }

  // image
  const auto texSrc = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(
                          PHANDLE, "plugin:imgborders:image")
                          ->getDataStaticPtr();
  wordexp_t p;
  wordexp(*texSrc, &p, 0);
  std::string texSrcExpanded;
  for (size_t i = 0; i < p.we_wordc; i++)
    texSrcExpanded.append(p.we_wordv[i]);
  wordfree(&p);
  if (!std::filesystem::exists(texSrcExpanded)) {
    HyprlandAPI::addNotification(
        PHANDLE,
        std::format("[imgborders] {} image at doesn't exist", texSrcExpanded),
        CHyprColor{1.0, 0.1, 0.1, 1.0}, 5000);
    m_isEnabled = false;
    return;
  }

  // sizes
  const auto sizesStr = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(
                            PHANDLE, "plugin:imgborders:sizes")
                            ->getDataStaticPtr();
  if (!sizesStr || std::string(*sizesStr).empty()) {
    HyprlandAPI::addNotification(PHANDLE,
                                 "[imgborders] missing sizes in config",
                                 CHyprColor{1.0, 0.1, 0.1, 1.0}, 5000);
    m_isEnabled = false;                             
    return;
  }
  if (!parseInts(sizesStr, m_sizes)) {
    HyprlandAPI::addNotification(PHANDLE,
                                 "[imgborders] invalid sizes in config",
                                 CHyprColor{1.0, 0.1, 0.1, 1.0}, 5000);
    m_isEnabled = false;
    return;
  }

  // 7x7 horsizes
  const auto horsizesStr = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(
                            PHANDLE, "plugin:imgborders:horsizes")
                            ->getDataStaticPtr();
  if (!horsizesStr || std::string(*horsizesStr).empty()) {
    HyprlandAPI::addNotification(PHANDLE,
                                 "[imgborders] missing horsizes in config",
                                 CHyprColor{1.0, 0.1, 0.1, 1.0}, 5000);
    m_isEnabled = false;
    return;
  }
  if (!parseInts(horsizesStr, m_hor_sizes)) {
    HyprlandAPI::addNotification(PHANDLE,
                                 "[imgborders] invalid horsizes in config",
                                 CHyprColor{1.0, 0.1, 0.1, 1.0}, 5000);
    m_isEnabled = false;
    return;
  }

    // 7x7 versizes
  const auto versizesStr = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(
                            PHANDLE, "plugin:imgborders:versizes")
                            ->getDataStaticPtr();
  if (!versizesStr || std::string(*versizesStr).empty()) {
    HyprlandAPI::addNotification(PHANDLE,
                                 "[imgborders] missing versizes in config",
                                 CHyprColor{1.0, 0.1, 0.1, 1.0}, 5000);
    m_isEnabled = false;
    return;
  }
  if (!parseInts(versizesStr, m_ver_sizes)) {
    HyprlandAPI::addNotification(PHANDLE,
                                 "[imgborders] invalid versizes in config",
                                 CHyprColor{1.0, 0.1, 0.1, 1.0}, 5000);
    m_isEnabled = false;
    return;
  }

  // Independent edge placements
  const auto topplacementsStr = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(
                            PHANDLE, "plugin:imgborders:topplacements")
                            ->getDataStaticPtr();
  if (!topplacementsStr || std::string(*topplacementsStr).empty()) {
    HyprlandAPI::addNotification(PHANDLE,
                                 "[imgborders] missing topplacements in config",
                                 CHyprColor{1.0, 0.1, 0.1, 1.0}, 5000);
    m_isEnabled = false;
    return;
  }
  if (!parseInts(topplacementsStr, m_top_placements)) {
    HyprlandAPI::addNotification(PHANDLE,
                                 "[imgborders] invalid topplacements in config",
                                 CHyprColor{1.0, 0.1, 0.1, 1.0}, 5000);
    m_isEnabled = false;
    return;
  }

  const auto bottomplacementsStr = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(
                            PHANDLE, "plugin:imgborders:bottomplacements")
                            ->getDataStaticPtr();
  if (!bottomplacementsStr || std::string(*bottomplacementsStr).empty()) {
    HyprlandAPI::addNotification(PHANDLE,
                                 "[imgborders] missing bottomplacements in config",
                                 CHyprColor{1.0, 0.1, 0.1, 1.0}, 5000);
    m_isEnabled = false;
    return;
  }
  if (!parseInts(bottomplacementsStr, m_bottom_placements)) {
    HyprlandAPI::addNotification(PHANDLE,
                                 "[imgborders] invalid bottomplacements in config",
                                 CHyprColor{1.0, 0.1, 0.1, 1.0}, 5000);
    m_isEnabled = false;
    return;
  }

  const auto leftplacementsStr = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(
                            PHANDLE, "plugin:imgborders:leftplacements")
                            ->getDataStaticPtr();
  if (!leftplacementsStr || std::string(*leftplacementsStr).empty()) {
    HyprlandAPI::addNotification(PHANDLE,
                                 "[imgborders] missing leftplacements in config",
                                 CHyprColor{1.0, 0.1, 0.1, 1.0}, 5000);
    m_isEnabled = false;
    return;
  }
  if (!parseInts(leftplacementsStr, m_left_placements)) {
    HyprlandAPI::addNotification(PHANDLE,
                                 "[imgborders] invalid leftplacements in config",
                                 CHyprColor{1.0, 0.1, 0.1, 1.0}, 5000);
    m_isEnabled = false;
    return;
  }

  const auto rightplacementsStr = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(
                            PHANDLE, "plugin:imgborders:rightplacements")
                            ->getDataStaticPtr();
  if (!rightplacementsStr || std::string(*rightplacementsStr).empty()) {
    HyprlandAPI::addNotification(PHANDLE,
                                 "[imgborders] missing rightplacements in config",
                                 CHyprColor{1.0, 0.1, 0.1, 1.0}, 5000);
    m_isEnabled = false;
    return;
  }
  if (!parseInts(rightplacementsStr, m_right_placements)) {
    HyprlandAPI::addNotification(PHANDLE,
                                 "[imgborders] invalid rightplacements in config",
                                 CHyprColor{1.0, 0.1, 0.1, 1.0}, 5000);
    m_isEnabled = false;
    return;
  }

  // insets
  const auto insetsStr = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(
                             PHANDLE, "plugin:imgborders:insets")
                             ->getDataStaticPtr();
  if (!insetsStr || std::string(*insetsStr).empty()) {
    HyprlandAPI::addNotification(
        PHANDLE,
        "[imgborders] missing insets in config. This should never happen!",
        CHyprColor{1.0, 0.1, 0.1, 1.0}, 5000);
    m_isEnabled = false;
    return;
  }
  if (!parseInts(insetsStr, m_insets)) {
    HyprlandAPI::addNotification(PHANDLE,
                                 "[imgborders] invalid insets in config",
                                 CHyprColor{1.0, 0.1, 0.1, 1.0}, 5000);
    m_isEnabled = false;
    return;
  }

  // scale
  m_scale = **(Hyprlang::FLOAT *const *)HyprlandAPI::getConfigValue(
                  PHANDLE, "plugin:imgborders:scale")
                  ->getDataStaticPtr();

  // smooth
  m_shouldSmooth = **(Hyprlang::INT *const *)HyprlandAPI::getConfigValue(
                         PHANDLE, "plugin:imgborders:smooth")
                         ->getDataStaticPtr();

  // blur
  m_shouldBlurGlobal = **(Hyprlang::INT *const *)HyprlandAPI::getConfigValue(
                             PHANDLE, "decoration:blur:enabled")
                             ->getDataStaticPtr();
  m_shouldBlur = **(Hyprlang::INT *const *)HyprlandAPI::getConfigValue(
                       PHANDLE, "plugin:imgborders:blur")
                       ->getDataStaticPtr();

  // Create textures
  // ------------

  // But first delete old textures
  if (m_tex_tl)
    m_tex_tl->destroyTexture();
  if (m_tex_tr)
    m_tex_tr->destroyTexture();
  if (m_tex_br)
    m_tex_br->destroyTexture();
  if (m_tex_bl)
    m_tex_bl->destroyTexture();

  if (m_tex_tle)
    m_tex_tle->destroyTexture();
  if (m_tex_tlc)
    m_tex_tlc->destroyTexture();
  if (m_tex_tme)
    m_tex_tme->destroyTexture();
  if (m_tex_trc)
    m_tex_trc->destroyTexture();
  if (m_tex_tre)
    m_tex_tre->destroyTexture();

  if (m_tex_rte)
    m_tex_rte->destroyTexture();
  if (m_tex_rtc)
    m_tex_rtc->destroyTexture();
  if (m_tex_rme)
    m_tex_rme->destroyTexture();
  if (m_tex_rbc)
    m_tex_rbc->destroyTexture();
  if (m_tex_rbe)
    m_tex_rbe->destroyTexture();
  
  if (m_tex_ble)
    m_tex_ble->destroyTexture();
  if (m_tex_blc)
    m_tex_blc->destroyTexture();
  if (m_tex_bme)
    m_tex_bme->destroyTexture();
  if (m_tex_brc)
    m_tex_brc->destroyTexture();
  if (m_tex_bre)
    m_tex_bre->destroyTexture();
  
  if (m_tex_lte)
    m_tex_lte->destroyTexture();
  if (m_tex_ltc)
    m_tex_ltc->destroyTexture();
  if (m_tex_lme)
    m_tex_lme->destroyTexture();
  if (m_tex_lbc)
    m_tex_lbc->destroyTexture();
  if (m_tex_lbe)
    m_tex_lbe->destroyTexture();

  if (m_tex_l)
    m_tex_l->destroyTexture();
  if (m_tex_t)
    m_tex_t->destroyTexture();
  if (m_tex_b)
    m_tex_b->destroyTexture();
  if (m_tex_r)
    m_tex_r->destroyTexture();
  m_tex_tl = nullptr;
  m_tex_tr = nullptr;
  m_tex_br = nullptr;
  m_tex_bl = nullptr;
  m_tex_l = nullptr;
  m_tex_t = nullptr;
  m_tex_b = nullptr;
  m_tex_r = nullptr;

  m_tex_tle = nullptr;
  m_tex_tlc = nullptr;
  m_tex_tme = nullptr;
  m_tex_trc = nullptr;
  m_tex_tre = nullptr;

  m_tex_rte = nullptr;
  m_tex_rtc = nullptr;
  m_tex_rme = nullptr;
  m_tex_rbc = nullptr;
  m_tex_rbe = nullptr;

  m_tex_ble = nullptr;
  m_tex_blc = nullptr;
  m_tex_bme = nullptr;
  m_tex_brc = nullptr;
  m_tex_bre = nullptr;

  m_tex_lte = nullptr;
  m_tex_ltc = nullptr;
  m_tex_lme = nullptr;
  m_tex_lbc = nullptr;
  m_tex_lbe = nullptr;

  auto tex = ImgUtils::load(texSrcExpanded);

  const auto BORDER_LEFT = (float)m_sizes[0];
  const auto BORDER_RIGHT = (float)m_sizes[1];
  const auto BORDER_TOP = (float)m_sizes[2];
  const auto BORDER_BOTTOM = (float)m_sizes[3];

  const auto WIDTH_MID = tex->m_size.x - BORDER_LEFT - BORDER_RIGHT;
  const auto HEIGHT_MID = tex->m_size.y - BORDER_TOP - BORDER_BOTTOM;

  const auto BORDER_VERTOPTOP = (float)m_ver_sizes[0];
  const auto BORDER_VERTOPBOT = (float)m_ver_sizes[1];
  const auto BORDER_VERBOTTOP = (float)m_ver_sizes[2];
  const auto BORDER_VERBOTBOT = (float)m_ver_sizes[3];
  
  const auto BORDER_HORLEFTLEFT = (float)m_hor_sizes[0];
  const auto BORDER_HORLEFTRIGHT = (float)m_hor_sizes[1];
  const auto BORDER_HORRIGHTLEFT = (float)m_hor_sizes[2];
  const auto BORDER_HORRIGHTRIGHT = (float)m_hor_sizes[3];


  m_tex_tl = ImgUtils::sliceTexture(
      tex, {{0., 0.}, 
            {BORDER_LEFT, BORDER_TOP}});

  m_tex_t  = ImgUtils::sliceTexture(
      tex, {{BORDER_LEFT, 0.}, 
            {WIDTH_MID, BORDER_TOP}});

  m_tex_tr = ImgUtils::sliceTexture(
      tex, {{tex->m_size.x - BORDER_RIGHT, 0.}, 
            {BORDER_RIGHT, BORDER_TOP}});

  m_tex_r  = ImgUtils::sliceTexture(
      tex, {{tex->m_size.x - BORDER_RIGHT, BORDER_TOP},
            {BORDER_RIGHT, tex->m_size.y - BORDER_TOP - BORDER_BOTTOM}});

  m_tex_br = ImgUtils::sliceTexture(
      tex, {{tex->m_size.x - BORDER_RIGHT, tex->m_size.y - BORDER_BOTTOM},
            {BORDER_RIGHT, BORDER_BOTTOM}});

  m_tex_b  = ImgUtils::sliceTexture(
      tex, {{BORDER_LEFT, tex->m_size.y - BORDER_BOTTOM},
            {tex->m_size.x - BORDER_LEFT - BORDER_RIGHT, BORDER_BOTTOM}});

  m_tex_bl = ImgUtils::sliceTexture(
      tex, {{0., tex->m_size.y - BORDER_BOTTOM}, 
            {BORDER_LEFT, BORDER_BOTTOM}});

  m_tex_l  = ImgUtils::sliceTexture(
      tex, {{0., BORDER_TOP},
            {BORDER_LEFT, tex->m_size.y - BORDER_TOP - BORDER_BOTTOM}});
// 7x7 FUNCTIONALITY
// TOP EDGE - Corrected definitions
  m_tex_tle = ImgUtils::sliceTexture(
      tex, {{BORDER_LEFT, 0.},
            {BORDER_HORLEFTLEFT, BORDER_TOP}});
  m_tex_tlc = ImgUtils::sliceTexture(
      tex, {{BORDER_LEFT + BORDER_HORLEFTLEFT, 0.},
            {BORDER_HORLEFTRIGHT, BORDER_TOP}});
  m_tex_tme = ImgUtils::sliceTexture(
      tex, {{BORDER_LEFT + BORDER_HORLEFTLEFT + BORDER_HORLEFTRIGHT, 0.},
            {tex->m_size.x - BORDER_RIGHT - BORDER_HORRIGHTRIGHT - BORDER_HORRIGHTLEFT - BORDER_LEFT - BORDER_HORLEFTLEFT - BORDER_HORLEFTRIGHT, BORDER_TOP}});
  m_tex_trc = ImgUtils::sliceTexture(
      tex, {{tex->m_size.x - BORDER_RIGHT - BORDER_HORRIGHTRIGHT - BORDER_HORRIGHTLEFT, 0.},
            {BORDER_HORRIGHTLEFT, BORDER_TOP}});
  m_tex_tre = ImgUtils::sliceTexture(
      tex, {{tex->m_size.x - BORDER_RIGHT - BORDER_HORRIGHTRIGHT, 0.},
            {BORDER_HORRIGHTRIGHT, BORDER_TOP}});
// RIGHT - Fixed coordinate calculations
  m_tex_rte = ImgUtils::sliceTexture(
      tex, {{tex->m_size.x - BORDER_RIGHT, BORDER_TOP},
            {BORDER_RIGHT, BORDER_VERTOPTOP}});
  m_tex_rtc = ImgUtils::sliceTexture(
      tex, {{tex->m_size.x - BORDER_RIGHT, BORDER_TOP + BORDER_VERTOPTOP},
            {BORDER_RIGHT, BORDER_VERTOPBOT}});
  m_tex_rme = ImgUtils::sliceTexture(
      tex, {{tex->m_size.x - BORDER_RIGHT, BORDER_TOP + BORDER_VERTOPTOP + BORDER_VERTOPBOT},
            {BORDER_RIGHT, tex->m_size.y - BORDER_BOTTOM - BORDER_VERBOTBOT - BORDER_VERBOTTOP - BORDER_TOP - BORDER_VERTOPTOP - BORDER_VERTOPBOT}});
  m_tex_rbc = ImgUtils::sliceTexture(
      tex, {{tex->m_size.x - BORDER_RIGHT, tex->m_size.y - BORDER_BOTTOM - BORDER_VERBOTBOT - BORDER_VERBOTTOP},
            {BORDER_RIGHT, BORDER_VERBOTTOP}});
  m_tex_rbe = ImgUtils::sliceTexture(
      tex, {{tex->m_size.x - BORDER_RIGHT, tex->m_size.y - BORDER_BOTTOM - BORDER_VERBOTBOT},
            {BORDER_RIGHT, BORDER_VERBOTBOT}});
// BOTTOM
  m_tex_ble = ImgUtils::sliceTexture(
      tex, {{BORDER_LEFT, tex->m_size.y - BORDER_BOTTOM},
            {BORDER_HORLEFTLEFT, BORDER_BOTTOM}});
  m_tex_blc = ImgUtils::sliceTexture(
      tex, {{BORDER_LEFT + BORDER_HORLEFTLEFT, tex->m_size.y - BORDER_BOTTOM},
            {BORDER_HORLEFTRIGHT, BORDER_BOTTOM}});
  m_tex_bme = ImgUtils::sliceTexture(
      tex, {{BORDER_LEFT + BORDER_HORLEFTLEFT + BORDER_HORLEFTRIGHT, tex->m_size.y - BORDER_BOTTOM},
            {tex->m_size.x - BORDER_RIGHT - BORDER_HORRIGHTRIGHT - BORDER_HORRIGHTLEFT - BORDER_LEFT - BORDER_HORLEFTLEFT - BORDER_HORLEFTRIGHT, BORDER_BOTTOM}});
  m_tex_brc = ImgUtils::sliceTexture(
      tex, {{tex->m_size.x - BORDER_RIGHT - BORDER_HORRIGHTRIGHT - BORDER_HORRIGHTLEFT, tex->m_size.y - BORDER_BOTTOM},
            {BORDER_HORRIGHTLEFT, BORDER_BOTTOM}});
  m_tex_bre = ImgUtils::sliceTexture(
      tex, {{tex->m_size.x - BORDER_RIGHT - BORDER_HORRIGHTRIGHT, tex->m_size.y - BORDER_BOTTOM},
            {BORDER_HORRIGHTRIGHT, BORDER_BOTTOM}});
// LEFT
  m_tex_lte = ImgUtils::sliceTexture(
      tex, {{0., BORDER_TOP},
            {BORDER_LEFT, BORDER_VERTOPTOP}});
  m_tex_ltc = ImgUtils::sliceTexture(
      tex, {{0., BORDER_TOP + BORDER_VERTOPTOP},
            {BORDER_LEFT, BORDER_VERTOPBOT}});
  m_tex_lme = ImgUtils::sliceTexture(
      tex, {{0., BORDER_TOP + BORDER_VERTOPTOP + BORDER_VERTOPBOT},
            {BORDER_LEFT, tex->m_size.y - BORDER_BOTTOM - BORDER_VERBOTBOT - BORDER_VERBOTTOP - BORDER_TOP - BORDER_VERTOPTOP - BORDER_VERTOPBOT}});
  m_tex_lbc = ImgUtils::sliceTexture(
      tex, {{0., tex->m_size.y - BORDER_BOTTOM - BORDER_VERBOTBOT - BORDER_VERBOTTOP},
            {BORDER_LEFT, BORDER_VERBOTTOP}});
  m_tex_lbe = ImgUtils::sliceTexture(
      tex, {{0., tex->m_size.y - BORDER_BOTTOM - BORDER_VERBOTBOT},
            {BORDER_LEFT, BORDER_VERBOTBOT}});

  tex->destroyTexture();

  g_pDecorationPositioner->repositionDeco(this);
}

void CImgBorder::updateRules() {
  const auto PWINDOW = m_pWindow.lock();
  auto rules = PWINDOW->m_matchedRules;
  auto prevIsHidden = m_isHidden;

  m_isHidden = false;
  for (auto &r : rules) {
    if (r->m_rule == "plugin:imgborders:noimgborders")
      m_isHidden = true;
  }

  if (prevIsHidden != m_isHidden)
    g_pDecorationPositioner->repositionDeco(this);
}

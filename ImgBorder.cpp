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
  g_pHyprOpenGL->m_renderData.discardOpacity = 0;

  // Corners

  if (m_tex_tl) { // Draw top-left FIRST
    const CBox box_tl = {box.pos(), {BORDER_LEFT, BORDER_TOP}};
      g_pHyprOpenGL->renderTexture(m_tex_tl, box_tl, a);
  }

  if (m_tex_tr) { // Draw top-right SECOND
    const CBox box_tr = {{box.x + box.width - BORDER_RIGHT, box.y},
                         {BORDER_RIGHT, BORDER_TOP}};
        g_pHyprOpenGL->renderTexture(m_tex_tr, box_tr, a);
  }
  
  if (m_tex_br) { // Draw bottom-right FIRST
    const CBox box_br = {
        {box.x + box.width - BORDER_RIGHT, box.y + box.height - BORDER_BOTTOM},
        {BORDER_RIGHT, BORDER_BOTTOM}};
      g_pHyprOpenGL->renderTexture(m_tex_br, box_br, a);
  }

  if (m_tex_bl) { // Draw bottom-left SECOND
    const CBox box_bl = {{box.x, box.y + box.height - BORDER_BOTTOM},
                         {BORDER_LEFT, BORDER_BOTTOM}};
      g_pHyprOpenGL->renderTexture(m_tex_bl, box_bl, a);
  }

  // Edges

  g_pHyprOpenGL->m_renderData.primarySurfaceUVTopLeft = {0, 0};

  g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
  if (m_tex_t != nullptr && m_tex_t->m_size.x != 0 && m_scale != 0) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight.x =
        WIDTH_MID / (m_tex_t->m_size.x * m_scale);
  }

  if (m_tex_t) {
    const CBox box_t = {{box.x + BORDER_LEFT, box.y}, {WIDTH_MID, BORDER_TOP}};
      g_pHyprOpenGL->renderTexture(m_tex_t, box_t, 
              {.a = a, .blur = shouldBlur(), .allowCustomUV = true, .wrapX = GL_REPEAT, .wrapY = GL_REPEAT});
  }

  if (m_tex_b) {
    const CBox box_b = {
        {box.x + BORDER_LEFT, box.y + box.height - BORDER_BOTTOM},
        {WIDTH_MID, BORDER_BOTTOM}};
      g_pHyprOpenGL->renderTexture(m_tex_b, box_b, 
              {.a = a, .blur = shouldBlur(), .allowCustomUV = true, .wrapX = GL_REPEAT, .wrapY = GL_REPEAT});
  }

  g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = {1., 1.};
  if (m_tex_l != nullptr && m_tex_l->m_size.y != 0 && m_scale != 0) {
    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight.y =
        HEIGHT_MID / (m_tex_l->m_size.y * m_scale);
  }

  if (m_tex_l) {
    const CBox box_l = {{box.x, box.y + BORDER_TOP}, {BORDER_LEFT, HEIGHT_MID}};
      g_pHyprOpenGL->renderTexture(m_tex_l, box_l, 
              {.a = a, .blur = shouldBlur(), .allowCustomUV = true, .wrapX = GL_REPEAT, .wrapY = GL_REPEAT});
  }

  if (m_tex_r) {
    const CBox box_r = {{box.x + box.width - BORDER_RIGHT, box.y + BORDER_TOP},
                        {BORDER_RIGHT, HEIGHT_MID}};
      g_pHyprOpenGL->renderTexture(m_tex_r, box_r, 
              {.a = a, .blur = shouldBlur(), .allowCustomUV = true, .wrapX = GL_REPEAT, .wrapY = GL_REPEAT});
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

bool parseInts(Hyprlang::STRING const *str, int outArr[4]) {
  auto strStream = std::stringstream(*str);
  int i = 0;
  for (; i < 4; i++) {
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
    return;
  }
  m_texSrc = texSrcExpanded;

  // sizes
  const auto sizesStr = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(
                            PHANDLE, "plugin:imgborders:sizes")
                            ->getDataStaticPtr();
  if (!sizesStr || std::string(*sizesStr).empty()) {
    HyprlandAPI::addNotification(PHANDLE,
                                 "[imgborders] missing sizes in config",
                                 CHyprColor{1.0, 0.1, 0.1, 1.0}, 5000);
    return;
  }
  if (!parseInts(sizesStr, m_sizes)) {
    HyprlandAPI::addNotification(PHANDLE,
                                 "[imgborders] invalid sizes in config",
                                 CHyprColor{1.0, 0.1, 0.1, 1.0}, 5000);
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
    return;
  }
  if (!parseInts(insetsStr, m_insets)) {
    HyprlandAPI::addNotification(PHANDLE,
                                 "[imgborders] invalid insets in config",
                                 CHyprColor{1.0, 0.1, 0.1, 1.0}, 5000);
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

  auto tex = ImgUtils::load(texSrcExpanded);

  const auto BORDER_LEFT = (float)m_sizes[0];
  const auto BORDER_RIGHT = (float)m_sizes[1];
  const auto BORDER_TOP = (float)m_sizes[2];
  const auto BORDER_BOTTOM = (float)m_sizes[3];

  const auto WIDTH_MID = tex->m_size.x - BORDER_LEFT - BORDER_RIGHT;
  const auto HEIGHT_MID = tex->m_size.y - BORDER_TOP - BORDER_BOTTOM;

  m_tex_tl = ImgUtils::sliceTexture(tex, {{0., 0.}, {BORDER_LEFT, BORDER_TOP}});

  m_tex_t =
      ImgUtils::sliceTexture(tex, {{BORDER_LEFT, 0.}, {WIDTH_MID, BORDER_TOP}});

  m_tex_tr = ImgUtils::sliceTexture(
      tex, {{tex->m_size.x - BORDER_RIGHT, 0.}, {BORDER_RIGHT, BORDER_TOP}});

  m_tex_r = ImgUtils::sliceTexture(
      tex, {{tex->m_size.x - BORDER_RIGHT, BORDER_TOP},
            {BORDER_RIGHT, tex->m_size.y - BORDER_TOP - BORDER_BOTTOM}});

  m_tex_br = ImgUtils::sliceTexture(
      tex, {{tex->m_size.x - BORDER_RIGHT, tex->m_size.y - BORDER_BOTTOM},
            {BORDER_RIGHT, BORDER_BOTTOM}});

  m_tex_b = ImgUtils::sliceTexture(
      tex, {{BORDER_LEFT, tex->m_size.y - BORDER_BOTTOM},
            {tex->m_size.x - BORDER_LEFT - BORDER_RIGHT, BORDER_BOTTOM}});

  m_tex_bl = ImgUtils::sliceTexture(
      tex, {{0., tex->m_size.y - BORDER_BOTTOM}, {BORDER_LEFT, BORDER_BOTTOM}});

  m_tex_l = ImgUtils::sliceTexture(
      tex, {{0., BORDER_TOP},
            {BORDER_LEFT, tex->m_size.y - BORDER_TOP - BORDER_BOTTOM}});

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

#pragma once

#include "globals.hpp"
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/WindowRule.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>

static const auto DISPLAY_NAME = "Image borders";

class CImgBorder : public IHyprWindowDecoration {
public:
  CImgBorder(PHLWINDOW);
  virtual ~CImgBorder();

  virtual SDecorationPositioningInfo getPositioningInfo();

  virtual void onPositioningReply(const SDecorationPositioningReply &reply);

  virtual void draw(PHLMONITOR, float const &a);

  bool shouldBlur();

  CBox getGlobalBoundingBox(PHLMONITOR pMonitor);

  void drawPass(PHLMONITOR, float const &a);

  virtual eDecorationType getDecorationType();

  virtual void updateWindow(PHLWINDOW);

  virtual void damageEntire();

  virtual eDecorationLayer getDecorationLayer();

  virtual uint64_t getDecorationFlags();

  virtual std::string getDisplayName() { return DISPLAY_NAME; }

  PHLWINDOW getWindow() { return m_pWindow.lock(); }

  void updateConfig();

  void updateRules();

  WP<CImgBorder> m_self;

private:
  PHLWINDOWREF m_pWindow;

  bool m_isEnabled;
  bool m_isHidden;
  int m_sizes[4];
  int m_insets[4];
  int m_hor_sizes[4];
  int m_ver_sizes[4];
  int m_hor_placements[2];
  int m_ver_placements[2];

  float m_scale;
  bool m_shouldSmooth;
  bool m_shouldBlurGlobal;
  bool m_shouldBlur;

  //corners
  SP<CTexture> m_tex_tl;
  SP<CTexture> m_tex_tr;
  SP<CTexture> m_tex_br;
  SP<CTexture> m_tex_bl;

  //sides
  SP<CTexture> m_tex_t;
  SP<CTexture> m_tex_r;
  SP<CTexture> m_tex_b;
  SP<CTexture> m_tex_l;

  //sides split up for even more custom borders
  
  SP<CTexture> m_tex_tle;
  SP<CTexture> m_tex_tlc;
  SP<CTexture> m_tex_tme;
  SP<CTexture> m_tex_trc;
  SP<CTexture> m_tex_tre;

  SP<CTexture> m_tex_rte;
  SP<CTexture> m_tex_rtc;
  SP<CTexture> m_tex_rme;
  SP<CTexture> m_tex_rbc;
  SP<CTexture> m_tex_rbe;
  
  SP<CTexture> m_tex_ble;
  SP<CTexture> m_tex_blc;
  SP<CTexture> m_tex_bme;
  SP<CTexture> m_tex_brc;
  SP<CTexture> m_tex_bre;

  SP<CTexture> m_tex_lte;
  SP<CTexture> m_tex_ltc;
  SP<CTexture> m_tex_lme;
  SP<CTexture> m_tex_lbc;
  SP<CTexture> m_tex_lbe;
  
  CBox m_bLastRelativeBox;
};

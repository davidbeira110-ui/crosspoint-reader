#include "CoverGridHomeUi.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "MappedInputManager.h"
#include "UITheme.h"
#include "icons/blocks.h"
#include "icons/book.h"
#include "icons/folder.h"
#include "icons/library.h"
#include "icons/settings2.h"
#include "icons/transfer.h"
#include "util/BookProgress.h"

namespace fui = freeink::ui;
namespace {
constexpr fui::ActionId SELECT = 1;
// Grid cell padding around each cover; also feeds the screen's horizontal
// inset so the cover columns land on the header chrome's inset line.
constexpr int16_t COVER_CELL_INSET = 6;
// Title plus the ornamental rule beneath it.
constexpr int16_t TITLE_ORNAMENT_HEIGHT = 18;
}  // namespace

CoverGridHomeUi::CoverGridHomeUi(GfxRenderer& renderer)
    : UiAppHost(renderer), coverCache(renderer), renderer(renderer) {}

void CoverGridHomeUi::begin(const std::vector<RecentBook>& recent, bool opds, bool continuing) {
  books = &recent;
  hasOpds = opds;
  hasContinueReading = continuing;
  if (!recent.empty()) coverCache.begin();
  resetUi();
  app.on(SELECT, &CoverGridHomeUi::onAction, this);
  app.setScreen(&CoverGridHomeUi::screenFn, this);
  refreshCoverPaths();
  progress = hasContinueReading && !books->empty() ? loadBookProgress(books->front().path) : -1;
  if (progress >= 0) snprintf(progressText, sizeof(progressText), "%d%%", progress);
}

void CoverGridHomeUi::refreshCoverPaths() {
  coverCache.invalidate();
  for (size_t i = 0; i < books->size() && i < coverPaths.size(); ++i) refreshCoverPath(i);
}

void CoverGridHomeUi::refreshCoverPath(size_t index) {
  if (index >= books->size() || index >= coverPaths.size()) return;
  coverCache.invalidate(index);
  coverPaths[index] =
      thumbHeight > 0 ? UITheme::getCoverThumbPath((*books)[index].coverBmpPath, thumbHeight) : std::string();
}

int CoverGridHomeUi::thumbHeightFor() const { return thumbHeight > 0 ? thumbHeight : THUMB_HEIGHT; }

bool CoverGridHomeUi::takeThumbHeightChanged() { return std::exchange(thumbHeightChanged, false); }

void CoverGridHomeUi::noteThumbHeight(int slotHeight) {
  // One shared height for every slot (hero and grid covers are the same
  // size). The +8 surplus gives the paint clip a little bleed so the art's
  // rightward nudge never exposes the left edge; covers narrower than the
  // 2:3 slot may show a thin sliver at the sides rather than being cropped.
  const int height = std::max(1, slotHeight + 8);
  if (thumbHeight != height) {
    thumbHeight = height;
    thumbHeightChanged = true;
    refreshCoverPaths();
  }
}

void CoverGridHomeUi::onAction(const fui::ActionEvent& event, void* user) {
  auto& self = *static_cast<CoverGridHomeUi*>(user);
  self.pending = event.value;
  self.app.clearTapFlash();
}

int CoverGridHomeUi::selectedAction(const MappedInputManager& input) {
  pending = -1;
  const auto touch = routeTouch(input);
  return touch.snap.touchReleased ? pending : -1;
}

void CoverGridHomeUi::screenFn(UiScreen& screen, void* user) { static_cast<CoverGridHomeUi*>(user)->draw(screen); }

void CoverGridHomeUi::draw(UiScreen& screen) {
  coverCache.prepare();
  const auto& theme = screen.theme();
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true);
  // Sides clear the rounded corners like the header, so the grid lines up under it.
  const int corner = BoardConfig::ACTIVE.cornerRadius;
  const auto left = static_cast<int16_t>(std::max(safe.x, corner));
  const auto right = static_cast<int16_t>(std::max(renderer.getScreenWidth() - safe.x - safe.width, corner));
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(safe.y), right,
                                                static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height),
                                                left});
  // Horizontal inset sized so the outer cover columns (content x plus the
  // grid's cell inset) sit on the theme's side-padding line: the heading and
  // tabs then align with the clock and battery, which tuck a few px further
  // in (headerStatusInset's optical bias).
  const int16_t hInset = std::max<int16_t>(
      0, static_cast<int16_t>(UITheme::getInstance().getMetrics().headerSidePadding - COVER_CELL_INSET - left));
  // Drop the status band, hero, and grid down: touch boards get a full step
  // (no button-hint band to fit), button boards a modest nudge. The tabs stay
  // put (bottom-anchored below).
  const int16_t topInset =
      BoardConfig::hasTouch() ? static_cast<int16_t>(theme.spaceLg) : static_cast<int16_t>(theme.spaceSm + 4);
  screen.insetContent(fui::Insets{topInset, hInset, theme.spaceSm, hInset});
  const bool landscape = renderer.getScreenWidth() > renderer.getScreenHeight();
  // Reserve the band's slot in the flow (its content draws at a fixed screen
  // position in drawHeaderBand); the slot doubles as padding above the heading.
  screen.takeTop(UITheme::getInstance().getMetrics().batteryBarHeight, theme.spaceSm);
  // Button boards run tighter above the tabs to make room for the top step.
  const int16_t tabGap = BoardConfig::hasTouch() ? theme.spaceSm : static_cast<int16_t>(4);
  const auto& board = BoardConfig::ACTIVE;
  auto tabRect = screen.takeBottom(UITheme::getInstance().getMetrics().coverGridTabBarHeight, tabGap);
  // The board's tab height sits at the bottom of the theme's slot, then its
  // offset lower; the slot itself is unchanged, so the grid keeps its size.
  const auto tabHeight = static_cast<int16_t>(tabRect.height * board.tabBarHeightPct / 100);
  tabRect.y = static_cast<int16_t>(tabRect.y + tabRect.height - tabHeight + board.tabBarOffsetY);
  tabRect.height = tabHeight;
  // The board's share of the row the tabs spread across, centered.
  const auto fitTabWidth = [&board](fui::Rect rect) {
    const auto width = static_cast<int16_t>(rect.width * board.tabBarWidthPct / 100);
    rect.x = static_cast<int16_t>(rect.x + (rect.width - width) / 2);
    rect.width = width;
    return rect;
  };
  if (books->empty()) {
    drawTabs(screen, fitTabWidth(tabRect.inset(fui::Insets{0, COVER_CELL_INSET, 0, COVER_CELL_INSET})));
    drawEmpty(screen);
    drawHeaderBand();
    return;
  }
  // Classic layout: a centered title plate with an ornamental rule, then two
  // rows of two large covers with their titles set beneath.
  const int16_t titleHeight = screen.target().lineHeight(theme.titleText.font);
  drawTitlePlate(screen, screen.takeTop(static_cast<int16_t>(titleHeight + TITLE_ORNAMENT_HEIGHT), theme.spaceSm));

  const fui::Rect body = screen.body();
  const int rowGap = std::max<int>(theme.spaceSm, body.height / 40);
  grid.gap = static_cast<int16_t>(std::max<int>(8, body.width / 20));
  grid.rowGap = static_cast<int16_t>(rowGap);
  grid.cellInset = fui::Insets{COVER_CELL_INSET, COVER_CELL_INSET, COVER_CELL_INSET, COVER_CELL_INSET};
  grid.titleText = theme.smallText;
  grid.titleText.align = fui::TextAlign::Center;
  grid.titleText.maxLines = 1;
  grid.labelHeight = screen.target().lineHeight(grid.titleText.font);
  grid.labelGap = 6;
  grid.labelFollowsCover = true;
  // Fit a 2:3 cover in both the row (minus its label) and the column. The
  // selection ring needs room on every side, hence the fixed reserves.
  const int rowHeight = std::max(1, (body.height - (GRID_ROWS - 1) * rowGap) / GRID_ROWS);
  const int cellWidth = (body.width - (GRID_COLUMNS - 1) * grid.gap) / GRID_COLUMNS;
  const int labelSpace = grid.labelHeight + grid.labelGap + 2 * COVER_CELL_INSET + 8;
  const int coverHeight =
      std::max(1, std::min(rowHeight - labelSpace, (cellWidth - 2 * COVER_CELL_INSET - 16) * 3 / 2));
  grid.coverSize.height = static_cast<int16_t>(coverHeight);
  grid.coverSize.width = static_cast<int16_t>(std::max(1, coverHeight * 2 / 3));
  grid.rowHeight = static_cast<int16_t>(coverHeight + labelSpace);
  noteThumbHeight(coverHeight);

  gridBounds = body;
  gridBounds.height =
      static_cast<int16_t>(std::min<int>(body.height, GRID_ROWS * grid.rowHeight + (GRID_ROWS - 1) * grid.rowGap));
  // Center the two rows vertically in the space between title and tabs.
  gridBounds.y = static_cast<int16_t>(body.y + (body.height - gridBounds.height) / 2);
  drawGrid(screen);
  drawTabs(screen, fitTabWidth(tabRect.inset(fui::Insets{0, COVER_CELL_INSET, 0, COVER_CELL_INSET})));
  drawHeaderBand();
  (void)landscape;
}

void CoverGridHomeUi::drawTitlePlate(UiScreen& screen, fui::Rect rect) {
  const auto& theme = screen.theme();
  auto title = theme.titleText;
  title.bold = true;
  title.align = fui::TextAlign::Center;
  const int16_t titleHeight = screen.target().lineHeight(title.font);
  screen.target().text(fui::Rect{rect.x, rect.y, rect.width, titleHeight}, tr(STR_LIBRARY), title);

  // Book-plate ornament: a double rule broken by a small diamond, the way a
  // printed book sets off a chapter heading.
  const auto ink = fui::Paint::solid(fui::Color::Black);
  const int16_t centerX = static_cast<int16_t>(rect.x + rect.width / 2);
  const int16_t ruleY = static_cast<int16_t>(rect.y + titleHeight + TITLE_ORNAMENT_HEIGHT / 2);
  const int16_t ruleHalf = static_cast<int16_t>(std::min<int>(rect.width / 3, 140));
  constexpr int16_t DIAMOND = 5;  // half-width of the diamond
  constexpr int16_t RULE_GAP = DIAMOND + 6;
  for (const int16_t side : {static_cast<int16_t>(-1), static_cast<int16_t>(1)}) {
    const int16_t inner = static_cast<int16_t>(centerX + side * RULE_GAP);
    const int16_t outer = static_cast<int16_t>(centerX + side * ruleHalf);
    const int16_t x = std::min(inner, outer);
    const auto w = static_cast<int16_t>(std::abs(outer - inner));
    screen.target().fill(fui::Rect{x, static_cast<int16_t>(ruleY - 2), w, 1}, ink);
    screen.target().fill(fui::Rect{x, static_cast<int16_t>(ruleY + 1), w, 1}, ink);
  }
  for (int16_t dy = -DIAMOND; dy <= DIAMOND; ++dy) {
    const auto half = static_cast<int16_t>(DIAMOND - std::abs(dy));
    screen.target().fill(fui::Rect{static_cast<int16_t>(centerX - half), static_cast<int16_t>(ruleY + dy),
                                   static_cast<int16_t>(2 * half + 1), 1},
                         ink);
  }
}

void CoverGridHomeUi::drawHeaderBand() {
  // The stock full-width band at the theme's topPadding, exactly like every
  // pushed screen's header: the battery/clock hold one position across the
  // whole UI, and the grid is widened to meet them (see the hInset above).
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.batteryBarHeight}, nullptr);
}

void CoverGridHomeUi::drawEmpty(UiScreen& screen) {
  const auto& theme = screen.theme();
  const auto body = screen.body();
  auto title = theme.titleText;
  title.bold = true;
  title.align = fui::TextAlign::Center;
  auto message = theme.bodyText;
  message.align = fui::TextAlign::Center;
  constexpr int16_t ICON_SIZE = 32;
  const int16_t titleHeight = screen.target().lineHeight(title.font);
  const int16_t messageHeight = screen.target().lineHeight(message.font);
  const int16_t contentHeight = ICON_SIZE + theme.spaceLg + titleHeight + theme.spaceSm + messageHeight;
  int16_t y = body.y + std::max(0, (body.height - contentHeight) / 2);
  renderer.drawIcon(BookIcon, body.x + (body.width - ICON_SIZE) / 2, y, ICON_SIZE);
  y += ICON_SIZE + theme.spaceLg;
  screen.target().text(fui::Rect{body.x, y, body.width, titleHeight}, tr(STR_NO_OPEN_BOOK), title);
  y += titleHeight + theme.spaceSm;
  screen.target().text(fui::Rect{body.x, y, body.width, messageHeight}, tr(STR_START_READING), message);
}

void CoverGridHomeUi::drawGrid(UiScreen& screen) {
  grid.count = static_cast<uint16_t>(std::min<size_t>(books->size(), MAX_BOOKS));
  grid.columns = GRID_COLUMNS;
  grid.columnLayout = fui::CoverGridColumnLayout::EqualWidth;
  grid.action = SELECT;
  grid.inputMask = fui::InputTouch;
  grid.selectedIndex = selected >= 0 && selected < static_cast<int>(grid.count) ? selected : -1;
  // A thick ring around the selected cover; a cell tint is easy to miss behind
  // dark cover art.
  grid.selectionIndicator = fui::CoverGridSelectionIndicator::CoverFrame;
  grid.selectedCoverFrameGap = 6;
  grid.selectedCoverFrameWidth = 6;
  grid.cellStyles = screen.theme().listRow;
  grid.cellStyles.selected.border = fui::Paint::solid(fui::Color::Black);
  grid.cellStyles.selected.foreground = fui::Paint::solid(fui::Color::Black);
  grid.cellStyles.active = grid.cellStyles.selected;
  grid.scrollIndicator = false;
  grid.itemProviderUserData = this;
  grid.itemProvider = [](uint16_t index, void* user) {
    const auto& self = *static_cast<const CoverGridHomeUi*>(user);
    return fui::coverGridItem((*self.books)[index].title.c_str(), index);
  };
  grid.coverPainterUserData = this;
  grid.coverPainter = [](fui::DrawTarget& target, fui::Rect cover, const fui::CoverGridItem&, uint16_t index,
                         void* user) {
    return static_cast<CoverGridHomeUi*>(user)->paintFramedCover(target, cover, index);
  };
  fui::coverGrid(screen.frame(), gridBounds, grid);
}

void CoverGridHomeUi::drawTabs(UiScreen& screen, fui::Rect rect) {
  static constexpr const uint8_t* ICONS[] = {FolderIcon, LibraryIcon, BlocksIcon, TransferIcon, Settings2Icon};
  int count = 0;
  for (int i = 0; i < 5; ++i) {
    if (i == 2 && !hasOpds) continue;
    auto& tab = tabItems[count];
    tab.value = books->size() + count;
    tab.selected = selected == tab.value;
    tab.label = nullptr;
    ++count;
  }
  tabs.tabs = tabItems.data();
  tabs.count = count;
  tabs.layout = fui::TabBarLayout::SpaceBetween;
  tabs.action = SELECT;
  tabs.inputMask = fui::InputTouch;
  tabs.iconSize = 32;
  tabs.iconPainterUserData = this;
  tabs.iconPainter = [](fui::DrawTarget&, fui::Rect iconRect, const fui::TabItem& tab, uint8_t, void* user) {
    const auto& self = *static_cast<CoverGridHomeUi*>(user);
    const int index = tab.value - static_cast<int>(self.books->size());
    const int icon = !self.hasOpds && index >= 2 ? index + 1 : index;
    self.renderer.drawIcon(ICONS[icon], iconRect.x, iconRect.y, iconRect.width);
    return true;
  };
  // No pill fill; the underline marks the selected tab.
  tabs.tabStyles.explicitlySet = true;
  tabs.selectedUnderline = 2;
  tabs.distributedSlotWidth = 0;
  fui::tabBar(screen.frame(), rect, tabs);
}

bool CoverGridHomeUi::paintFramedCover(fui::DrawTarget& target, fui::Rect rect, size_t index) {
  constexpr int16_t SHADOW_OFFSET = 2;
  const auto ink = fui::Paint::solid(fui::Color::Black);
  target.fill(fui::Rect{rect.right(), static_cast<int16_t>(rect.y + SHADOW_OFFSET), SHADOW_OFFSET, rect.height}, ink);
  target.fill(fui::Rect{static_cast<int16_t>(rect.x + SHADOW_OFFSET), rect.bottom(), rect.width, SHADOW_OFFSET}, ink);
  // The false spine draws inside the cover paint (HomeCoverCache), glued to
  // the art's left edge, so it stays aligned whatever each cover's margin is.
  const bool drawn = index < coverPaths.size() && coverCache.paint(rect, index, coverPaths[index]);
  return drawn;
}

#ifndef PAUSE_MENU_H
#define PAUSE_MENU_H

// The pause-style settings menu: the game freezes and blurs behind a bevelled panel, the five menus are
// pages turned with L / R, and every setting sits in a slot with a cursor and a description plate.
//
// This class is the renderer and its state. It draws whatever a Source gives it; SohMenu is the Source
// and builds the rows from the menu's WidgetInfo data.

#include "PmModel.h"
#include <memory>
#include <string>
#include <vector>

namespace Pm {

class Source {
  public:
    virtual ~Source() = default;
    virtual const std::vector<std::string>& Pages() = 0;
    virtual const std::vector<std::string>& Sections(const std::string& page) = 0;
    // Persisted page / section.
    virtual std::string CurrentPage() = 0;
    virtual void SetCurrentPage(const std::string& page) = 0;
    virtual std::string CurrentSection(const std::string& page) = 0;
    virtual void SetCurrentSection(const std::string& page, const std::string& section) = 0;
    virtual Section Build(const std::string& page, const std::string& section) = 0;
    // Number of enabled toggles in a section, for the sidebar.
    virtual int CountOn(const std::string& page, const std::string& section) = 0;
    // Fills `out` with one group per "Page > Section" that has rows whose label contains `query`.
    virtual int Search(const std::string& query, std::vector<Group>& out, int limit) = 0;
    virtual void Close() = 0;
    virtual void Quit() = 0;
    virtual void Reset() = 0;
};

class PauseMenu {
  public:
    explicit PauseMenu(Source* source);
    ~PauseMenu();
    // Every frame, open or not.
    void Update(bool visible);
    // While the menu is open.
    void Draw();
    // True from the first frame the menu is drawn until it closes.
    bool IsOpen() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Pm

#endif // PAUSE_MENU_H

#include "paint_area.hpp"

#include <cctype>
#include <codecvt>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <locale>
#include <optional>
#include <string>
#include <utility>

#include <cppurses/painter/brush.hpp>
#include <cppurses/painter/color.hpp>
#include <cppurses/painter/glyph.hpp>
#include <cppurses/painter/glyph_string.hpp>
#include <cppurses/painter/painter.hpp>
#include <cppurses/painter/trait.hpp>
#include <cppurses/system/key.hpp>
#include <cppurses/system/mouse.hpp>
#include <cppurses/widget/border.hpp>
#include <cppurses/widget/focus_policy.hpp>
#include <cppurses/widget/point.hpp>
#include <cppurses/widget/widget.hpp>

#include <signals/slot.hpp>

using namespace cppurses;

namespace {

void insert_newline(Point first, Point second, std::ostream& os)
{
    if (first.y >= second.y)
        return;
    std::string newlines(second.y - first.y, '\n');
    os << newlines;
}

void insert_space(Point first, Point second, std::ostream& os)
{
    std::size_t spaces_n{second.x};
    if (first.y == second.y)
        spaces_n -= first.x + 1;
    std::string spaces(spaces_n, ' ');
    os << spaces;
}

}  // namespace

namespace demos {
namespace glyph_paint {

Paint_area::Paint_area()
{
    this->focus_policy = Focus_policy::Strong;

    this->border.enable();
    this->border.segments.disable_all();
    this->border.segments.east.enable();
}

void Paint_area::set_glyph(Glyph glyph)
{
    current_glyph_ = std::move(glyph);
    glyph_changed(current_glyph_);
    if (erase_enabled_) {
        this->disable_erase();
        erase_disabled();
    }
}

void Paint_area::set_symbol(const Glyph& sym)
{
    if (erase_enabled_) {
        this->disable_erase();
        erase_disabled();
    }
    current_glyph_.symbol = sym.symbol;
    std::optional<Color> sym_bg{sym.brush.background_color()};
    if (sym_bg)
        current_glyph_.brush.set_background(*sym_bg);
    std::optional<Color> sym_fg{sym.brush.foreground_color()};
    if (sym_fg)
        current_glyph_.brush.set_foreground(*sym_fg);
    glyph_changed(current_glyph_);
}

void Paint_area::set_foreground_color(Color c)
{
    current_glyph_.brush.set_foreground(c);
    if (!erase_enabled_)
        glyph_changed(current_glyph_);
}

void Paint_area::set_background_color(Color c)
{
    current_glyph_.brush.set_background(c);
    if (!erase_enabled_)
        glyph_changed(current_glyph_);
}

void Paint_area::set_trait(Trait t)
{
    current_glyph_.brush.add_traits(t);
    if (!erase_enabled_)
        glyph_changed(current_glyph_);
}

void Paint_area::remove_traits(Trait t)
{
    current_glyph_.brush.remove_traits(t);
    if (!erase_enabled_)
        glyph_changed(current_glyph_);
}

void Paint_area::enable_erase()
{
    erase_enabled_ = true;
    glyph_changed(L' ');
}

void Paint_area::disable_erase()
{
    erase_enabled_ = false;
    glyph_changed(current_glyph_);
}

void Paint_area::enable_grid()
{
    this->set_wallpaper(L'┼'_g | foreground(Color::Dark_gray));
    this->update();
}

void Paint_area::disable_grid()
{
    this->set_wallpaper(std::nullopt);
    this->update();
}

void Paint_area::clear()
{
    glyphs_painted_.clear();
    this->update();
}

Glyph Paint_area::glyph() const { return current_glyph_; }

void Paint_area::toggle_clone() { clone_enabled_ = !clone_enabled_; }

void Paint_area::write(std::ostream& os)
{
    Point previous_nl{0, 0};
    Point previous_s{0, static_cast<std::size_t>(-1)};
    for (const auto& cg_pair : glyphs_painted_) {
        insert_newline(previous_nl, cg_pair.first, os);
        insert_space(previous_s, cg_pair.first, os);
        os << cppurses::utility::wchar_to_bytes(cg_pair.second.symbol);
        previous_nl = cg_pair.first;
        previous_s  = cg_pair.first;
    }
}

void Paint_area::read(std::istream& is)
{
    this->clear();
    Point current{0, 0};
    is >> std::noskipws;
    std::string file_text{std::istream_iterator<char>{is},
                          std::istream_iterator<char>()};
    Glyph_string file_glyphs{file_text};
    for (const Glyph& glyph : file_glyphs) {
        const wchar_t sym{glyph.symbol};
        if (sym != L' ' and sym != L'\n' and sym != L'\r')
            glyphs_painted_[current] = glyph;
        ++current.x;
        if (sym == L'\n') {
            ++current.y;
            current.x = 0;
        }
    }
}

bool Paint_area::paint_event()
{
    Painter p{*this};
    for (const auto& gc_pair : glyphs_painted_) {
        if (gc_pair.first.x < this->width() and
            gc_pair.first.y < this->height()) {
            p.put(gc_pair.second, gc_pair.first);
        }
    }
    return Widget::paint_event();
}

bool Paint_area::mouse_press_event(const Mouse& m)
{
    if (m.button == Mouse::Button::Right)
        this->remove_glyph(m.local);
    else if (m.button == Mouse::Button::Middle) {
        if (glyphs_painted_.count(m.local) == 1)
            this->set_glyph(glyphs_painted_[m.local]);
    }
    else
        this->place_glyph(m.local.x, m.local.y);
    return Widget::mouse_press_event(m);
}

bool Paint_area::key_press_event(Key k)
{
    auto const symbol = to_wchar(k);
    if (!this->cursor.enabled()) {
        if (!std::iscntrl(symbol))
            this->set_symbol(symbol);
        return Widget::key_press_event(k);
    }
    if (this->width() == 0 || this->height() == 0)
        return Widget::key_press_event(k);
    std::size_t new_x{this->cursor.x() + 1};
    std::size_t new_y{this->cursor.y() + 1};
    switch (k) {
        case Key::Arrow_right:
            if (new_x == this->width())
                new_x = 0;
            this->cursor.set_x(new_x);
            break;
        case Key::Arrow_left: this->cursor.set_x(this->cursor.x() - 1); break;
        case Key::Arrow_down:
            if (new_y == this->height())
                new_y = 0;
            this->cursor.set_y(new_y);
            break;
        case Key::Arrow_up: this->cursor.set_y(this->cursor.y() - 1); break;
        case Key::Enter:
            this->place_glyph(this->cursor.x(), this->cursor.y());
            break;
        default:
            if (!std::iscntrl(symbol)) {
                this->set_symbol(symbol);
                this->place_glyph(this->cursor.x(), this->cursor.y());
                this->update();
            }
            break;
    }
    return Widget::key_press_event(k);
}

void Paint_area::place_glyph(std::size_t x, std::size_t y)
{
    if (clone_enabled_) {
        if (glyphs_painted_.count(Point{x, y}) == 1) {
            this->set_glyph(glyphs_painted_[Point{x, y}]);
            this->toggle_clone();
        }
    }
    else if (erase_enabled_)
        this->remove_glyph(Point{x, y});
    else {
        glyphs_painted_[Point{x, y}] = current_glyph_;
        this->update();
    }
}

void Paint_area::remove_glyph(Point coords)
{
    glyphs_painted_.erase(coords);
    this->update();
}

namespace slot {

sig::Slot<void(Glyph)> set_glyph(Paint_area& pa)
{
    sig::Slot<void(Glyph)> slot{[&pa](Glyph g) { pa.set_glyph(std::move(g)); }};
    slot.track(pa.destroyed);
    return slot;
}

sig::Slot<void()> set_glyph(Paint_area& pa, const Glyph& glyph)
{
    sig::Slot<void()> slot{[&pa, glyph] { pa.set_glyph(glyph); }};
    slot.track(pa.destroyed);
    return slot;
}

sig::Slot<void(Glyph)> set_symbol(Paint_area& pa)
{
    sig::Slot<void(Glyph)> slot{
        [&pa](Glyph symbol) { pa.set_symbol(std::move(symbol)); }};
    slot.track(pa.destroyed);
    return slot;
}

sig::Slot<void()> set_symbol(Paint_area& pa, const Glyph& symbol)
{
    sig::Slot<void()> slot{[&pa, symbol] { pa.set_symbol(symbol); }};
    slot.track(pa.destroyed);
    return slot;
}

sig::Slot<void(Color)> set_foreground_color(Paint_area& pa)
{
    sig::Slot<void(Color)> slot{[&pa](Color c) { pa.set_foreground_color(c); }};
    slot.track(pa.destroyed);
    return slot;
}

sig::Slot<void()> set_foreground_color(Paint_area& pa, Color c)
{
    sig::Slot<void()> slot{[&pa, c] { pa.set_foreground_color(c); }};
    slot.track(pa.destroyed);
    return slot;
}

sig::Slot<void(Color)> set_background_color(Paint_area& pa)
{
    sig::Slot<void(Color)> slot{[&pa](Color c) { pa.set_background_color(c); }};
    slot.track(pa.destroyed);
    return slot;
}

sig::Slot<void()> set_background_color(Paint_area& pa, Color c)
{
    sig::Slot<void()> slot{[&pa, c] { pa.set_background_color(c); }};
    slot.track(pa.destroyed);
    return slot;
}

sig::Slot<void(Trait)> set_trait(Paint_area& pa)
{
    sig::Slot<void(Trait)> slot{[&pa](Trait t) { pa.set_trait(t); }};
    slot.track(pa.destroyed);
    return slot;
}

sig::Slot<void()> set_trait(Paint_area& pa, Trait t)
{
    sig::Slot<void()> slot{[&pa, t] { pa.set_trait(t); }};
    slot.track(pa.destroyed);
    return slot;
}

sig::Slot<void(Trait)> remove_traits(Paint_area& pa)
{
    sig::Slot<void(Trait)> slot{[&pa](Trait t) { pa.remove_traits(t); }};
    slot.track(pa.destroyed);
    return slot;
}

sig::Slot<void()> remove_traits(Paint_area& pa, Trait t)
{
    sig::Slot<void()> slot{[&pa, t] { pa.remove_traits(t); }};
    slot.track(pa.destroyed);
    return slot;
}

sig::Slot<void()> toggle_clone(Paint_area& pa)
{
    sig::Slot<void()> slot{[&pa] { pa.toggle_clone(); }};
    slot.track(pa.destroyed);
    return slot;
}

sig::Slot<void()> clear(Paint_area& pa)
{
    sig::Slot<void()> slot{[&pa] { pa.clear(); }};
    slot.track(pa.destroyed);
    return slot;
}

sig::Slot<void()> enable_erase(Paint_area& pa)
{
    sig::Slot<void()> slot{[&pa] { pa.enable_erase(); }};
    slot.track(pa.destroyed);
    return slot;
}

sig::Slot<void()> disable_erase(Paint_area& pa)
{
    sig::Slot<void()> slot{[&pa] { pa.disable_erase(); }};
    slot.track(pa.destroyed);
    return slot;
}

sig::Slot<void()> enable_grid(Paint_area& pa)
{
    sig::Slot<void()> slot{[&pa] { pa.enable_grid(); }};
    slot.track(pa.destroyed);
    return slot;
}

sig::Slot<void()> disable_grid(Paint_area& pa)
{
    sig::Slot<void()> slot{[&pa] { pa.disable_grid(); }};
    slot.track(pa.destroyed);
    return slot;
}

sig::Slot<void(std::ostream&)> write(Paint_area& pa)
{
    sig::Slot<void(std::ostream&)> slot{
        [&pa](std::ostream& os) { pa.write(os); }};
    slot.track(pa.destroyed);
    return slot;
}

sig::Slot<void(std::istream&)> read(Paint_area& pa)
{
    sig::Slot<void(std::istream&)> slot{
        [&pa](std::istream& is) { pa.read(is); }};
    slot.track(pa.destroyed);
    return slot;
}

}  // namespace slot
}  // namespace glyph_paint
}  // namespace demos

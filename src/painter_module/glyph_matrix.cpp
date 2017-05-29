#include "painter_module/glyph_matrix.hpp"
#include "painter_module/glyph.hpp"
#include <cstddef>
#include <vector>

namespace cppurses {
Glyph_matrix::Glyph_matrix(std::size_t width, std::size_t height)
    : matrix_{height, std::vector<Glyph>(width, Glyph(" "))} {}

void Glyph_matrix::resize(std::size_t width, std::size_t height) {
    matrix_.resize(height);
    for (auto& row : matrix_) {
        row.resize(width, Glyph(" "));
    }
}

std::size_t Glyph_matrix::width() const {
    return matrix_.empty() ? 0 : matrix_.at(0).size();
}

std::size_t Glyph_matrix::height() const {
    return matrix_.size();
}

Glyph& Glyph_matrix::at(std::size_t x, std::size_t y) {
    return this->at_impl(*this, x, y);
}

const Glyph& Glyph_matrix::at(std::size_t x, std::size_t y) const {
    return this->at_impl(*this, x, y);
}

}  // namespace cppurses

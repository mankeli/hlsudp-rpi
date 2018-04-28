// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Copyright (C) 2017 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://gnu.org/licenses/gpl-2.0.txt>

#include "multiplex-mappers-internal.h"

namespace rgb_matrix {
namespace internal {
// A Pixel Mapper maps physical pixels locations to the internal logical
// mapping in a panel or panel-assembly, which depends on the wiring.
class MultiplexMapperBase : public MultiplexMapper {
public:
  MultiplexMapperBase(const char *name, int stretch_factor)
    : name_(name), panel_stretch_factor_(stretch_factor) {}

  // This method is const, but we sneakily remember the original size
  // of the panels so that we can more easily quantize things.
  // So technically, we're stateful, but let's pretend we're not changing
  // state. In the context this is used, it is never accessed in multiple
  // threads.
  virtual void EditColsRows(int *cols, int *rows) const {
    panel_rows_ = *rows;
    panel_cols_ = *cols;

    *rows /= panel_stretch_factor_;
    *cols *= panel_stretch_factor_;
  }

  virtual bool GetSizeMapping(int matrix_width, int matrix_height,
                              int *visible_width, int *visible_height) const {
    // Matrix width has been altered. Alter it back.
    *visible_width = matrix_width / panel_stretch_factor_;
    *visible_height = matrix_height * panel_stretch_factor_;
    return true;
  }

  virtual const char *GetName() const { return name_; }

  // The MapVisibleToMatrix() as required by PanelMatrix here does
  virtual void MapVisibleToMatrix(int matrix_width, int matrix_height,
                                  int visible_x, int visible_y,
                                  int *matrix_x, int *matrix_y) const {
    const int chained_panel  = visible_x / panel_cols_;
    const int parallel_panel = visible_y / panel_rows_;

    const int within_panel_x = visible_x % panel_cols_;
    const int within_panel_y = visible_y % panel_rows_;

    int new_x, new_y;
    MapSinglePanel(within_panel_x, within_panel_y, &new_x, &new_y);
    *matrix_x = chained_panel  * panel_stretch_factor_*panel_cols_ + new_x;
    *matrix_y = parallel_panel * panel_rows_/panel_stretch_factor_ + new_y;
  }

  // Map the coordinates for a single panel. This is to be overridden in
  // derived classes.
  virtual void MapSinglePanel(int visible_x, int visible_y,
                              int *matrix_x, int *matrix_y) const = 0;
protected:
  const char *const name_;
  const int panel_stretch_factor_;

  mutable int panel_cols_;
  mutable int panel_rows_;
};


/* ========================================================================
 * Multiplexer implementations.
 *
 * Extend MultiplexMapperBase and implement MapSinglePanel. You only have
 * to worry about the mapping within a single panel, the overall panel
 * construction with chains and parallel is already taken care of.
 *
 * Don't forget to register the new multiplexer sin CreateMultiplexMapperList()
 * below. After that, the new mapper is available in the --led-multiplexing
 * option.
 */
class DoubleAbsenMultiplexMapper : public MultiplexMapperBase {
public:
  DoubleAbsenMultiplexMapper() : MultiplexMapperBase("Absen", 1) {}

  void MapSinglePanel(int x, int y, int *matrix_x, int *matrix_y) const {
    const int mapping[64][16][2] = {
      { { 3, 0}, { 3, 1}, { 3, 2}, { 3, 3}, { 7, 0}, { 7, 1}, { 7, 2}, { 7, 3}, { 3, 4}, { 3, 5}, { 3, 6}, { 3, 7}, { 7, 4}, { 7, 5}, { 7, 6}, { 7, 7} },
      { { 2, 0}, { 2, 1}, { 2, 2}, { 2, 3}, { 6, 0}, { 6, 1}, { 6, 2}, { 6, 3}, { 2, 4}, { 2, 5}, { 2, 6}, { 2, 7}, { 6, 4}, { 6, 5}, { 6, 6}, { 6, 7} },
      { { 1, 0}, { 1, 1}, { 1, 2}, { 1, 3}, { 5, 0}, { 5, 1}, { 5, 2}, { 5, 3}, { 1, 4}, { 1, 5}, { 1, 6}, { 1, 7}, { 5, 4}, { 5, 5}, { 5, 6}, { 5, 7} },
      { { 0, 0}, { 0, 1}, { 0, 2}, { 0, 3}, { 4, 0}, { 4, 1}, { 4, 2}, { 4, 3}, { 0, 4}, { 0, 5}, { 0, 6}, { 0, 7}, { 4, 4}, { 4, 5}, { 4, 6}, { 4, 7} },
      { { 15, 0}, { 15, 1}, { 15, 2}, { 15, 3}, { 11, 0}, { 11, 1}, { 11, 2}, { 11, 3}, { 15, 4}, { 15, 5}, { 15, 6}, { 15, 7}, { 11, 4}, { 11, 5}, { 11, 6}, { 11, 7} },
      { { 14, 0}, { 14, 1}, { 14, 2}, { 14, 3}, { 10, 0}, { 10, 1}, { 10, 2}, { 10, 3}, { 14, 4}, { 14, 5}, { 14, 6}, { 14, 7}, { 10, 4}, { 10, 5}, { 10, 6}, { 10, 7} },
      { { 13, 0}, { 13, 1}, { 13, 2}, { 13, 3}, { 9, 0}, { 9, 1}, { 9, 2}, { 9, 3}, { 13, 4}, { 13, 5}, { 13, 6}, { 13, 7}, { 9, 4}, { 9, 5}, { 9, 6}, { 9, 7} },
      { { 12, 0}, { 12, 1}, { 12, 2}, { 12, 3}, { 8, 0}, { 8, 1}, { 8, 2}, { 8, 3}, { 12, 4}, { 12, 5}, { 12, 6}, { 12, 7}, { 8, 4}, { 8, 5}, { 8, 6}, { 8, 7} },
      { { 19, 0}, { 19, 1}, { 19, 2}, { 19, 3}, { 23, 0}, { 23, 1}, { 23, 2}, { 23, 3}, { 19, 4}, { 19, 5}, { 19, 6}, { 19, 7}, { 23, 4}, { 23, 5}, { 23, 6}, { 23, 7} },
      { { 18, 0}, { 18, 1}, { 18, 2}, { 18, 3}, { 22, 0}, { 22, 1}, { 22, 2}, { 22, 3}, { 18, 4}, { 18, 5}, { 18, 6}, { 18, 7}, { 22, 4}, { 22, 5}, { 22, 6}, { 22, 7} },
      { { 17, 0}, { 17, 1}, { 17, 2}, { 17, 3}, { 21, 0}, { 21, 1}, { 21, 2}, { 21, 3}, { 17, 4}, { 17, 5}, { 17, 6}, { 17, 7}, { 21, 4}, { 21, 5}, { 21, 6}, { 21, 7} },
      { { 16, 0}, { 16, 1}, { 16, 2}, { 16, 3}, { 20, 0}, { 20, 1}, { 20, 2}, { 20, 3}, { 16, 4}, { 16, 5}, { 16, 6}, { 16, 7}, { 20, 4}, { 20, 5}, { 20, 6}, { 20, 7} },
      { { 31, 0}, { 31, 1}, { 31, 2}, { 31, 3}, { 27, 0}, { 27, 1}, { 27, 2}, { 27, 3}, { 31, 4}, { 31, 5}, { 31, 6}, { 31, 7}, { 27, 4}, { 27, 5}, { 27, 6}, { 27, 7} },
      { { 30, 0}, { 30, 1}, { 30, 2}, { 30, 3}, { 26, 0}, { 26, 1}, { 26, 2}, { 26, 3}, { 30, 4}, { 30, 5}, { 30, 6}, { 30, 7}, { 26, 4}, { 26, 5}, { 26, 6}, { 26, 7} },
      { { 29, 0}, { 29, 1}, { 29, 2}, { 29, 3}, { 25, 0}, { 25, 1}, { 25, 2}, { 25, 3}, { 29, 4}, { 29, 5}, { 29, 6}, { 29, 7}, { 25, 4}, { 25, 5}, { 25, 6}, { 25, 7} },
      { { 28, 0}, { 28, 1}, { 28, 2}, { 28, 3}, { 24, 0}, { 24, 1}, { 24, 2}, { 24, 3}, { 28, 4}, { 28, 5}, { 28, 6}, { 28, 7}, { 24, 4}, { 24, 5}, { 24, 6}, { 24, 7} },
      { { 35, 0}, { 35, 1}, { 35, 2}, { 35, 3}, { 39, 0}, { 39, 1}, { 39, 2}, { 39, 3}, { 35, 4}, { 35, 5}, { 35, 6}, { 35, 7}, { 39, 4}, { 39, 5}, { 39, 6}, { 39, 7} },
      { { 34, 0}, { 34, 1}, { 34, 2}, { 34, 3}, { 38, 0}, { 38, 1}, { 38, 2}, { 38, 3}, { 34, 4}, { 34, 5}, { 34, 6}, { 34, 7}, { 38, 4}, { 38, 5}, { 38, 6}, { 38, 7} },
      { { 33, 0}, { 33, 1}, { 33, 2}, { 33, 3}, { 37, 0}, { 37, 1}, { 37, 2}, { 37, 3}, { 33, 4}, { 33, 5}, { 33, 6}, { 33, 7}, { 37, 4}, { 37, 5}, { 37, 6}, { 37, 7} },
      { { 32, 0}, { 32, 1}, { 32, 2}, { 32, 3}, { 36, 0}, { 36, 1}, { 36, 2}, { 36, 3}, { 32, 4}, { 32, 5}, { 32, 6}, { 32, 7}, { 36, 4}, { 36, 5}, { 36, 6}, { 36, 7} },
      { { 47, 0}, { 47, 1}, { 47, 2}, { 47, 3}, { 43, 0}, { 43, 1}, { 43, 2}, { 43, 3}, { 47, 4}, { 47, 5}, { 47, 6}, { 47, 7}, { 43, 4}, { 43, 5}, { 43, 6}, { 43, 7} },
      { { 46, 0}, { 46, 1}, { 46, 2}, { 46, 3}, { 42, 0}, { 42, 1}, { 42, 2}, { 42, 3}, { 46, 4}, { 46, 5}, { 46, 6}, { 46, 7}, { 42, 4}, { 42, 5}, { 42, 6}, { 42, 7} },
      { { 45, 0}, { 45, 1}, { 45, 2}, { 45, 3}, { 41, 0}, { 41, 1}, { 41, 2}, { 41, 3}, { 45, 4}, { 45, 5}, { 45, 6}, { 45, 7}, { 41, 4}, { 41, 5}, { 41, 6}, { 41, 7} },
      { { 44, 0}, { 44, 1}, { 44, 2}, { 44, 3}, { 40, 0}, { 40, 1}, { 40, 2}, { 40, 3}, { 44, 4}, { 44, 5}, { 44, 6}, { 44, 7}, { 40, 4}, { 40, 5}, { 40, 6}, { 40, 7} },
      { { 51, 0}, { 51, 1}, { 51, 2}, { 51, 3}, { 55, 0}, { 55, 1}, { 55, 2}, { 55, 3}, { 51, 4}, { 51, 5}, { 51, 6}, { 51, 7}, { 55, 4}, { 55, 5}, { 55, 6}, { 55, 7} },
      { { 50, 0}, { 50, 1}, { 50, 2}, { 50, 3}, { 54, 0}, { 54, 1}, { 54, 2}, { 54, 3}, { 50, 4}, { 50, 5}, { 50, 6}, { 50, 7}, { 54, 4}, { 54, 5}, { 54, 6}, { 54, 7} },
      { { 49, 0}, { 49, 1}, { 49, 2}, { 49, 3}, { 53, 0}, { 53, 1}, { 53, 2}, { 53, 3}, { 49, 4}, { 49, 5}, { 49, 6}, { 49, 7}, { 53, 4}, { 53, 5}, { 53, 6}, { 53, 7} },
      { { 48, 0}, { 48, 1}, { 48, 2}, { 48, 3}, { 52, 0}, { 52, 1}, { 52, 2}, { 52, 3}, { 48, 4}, { 48, 5}, { 48, 6}, { 48, 7}, { 52, 4}, { 52, 5}, { 52, 6}, { 52, 7} },
      { { 63, 0}, { 63, 1}, { 63, 2}, { 63, 3}, { 59, 0}, { 59, 1}, { 59, 2}, { 59, 3}, { 63, 4}, { 63, 5}, { 63, 6}, { 63, 7}, { 59, 4}, { 59, 5}, { 59, 6}, { 59, 7} },
      { { 62, 0}, { 62, 1}, { 62, 2}, { 62, 3}, { 58, 0}, { 58, 1}, { 58, 2}, { 58, 3}, { 62, 4}, { 62, 5}, { 62, 6}, { 62, 7}, { 58, 4}, { 58, 5}, { 58, 6}, { 58, 7} },
      { { 61, 0}, { 61, 1}, { 61, 2}, { 61, 3}, { 57, 0}, { 57, 1}, { 57, 2}, { 57, 3}, { 61, 4}, { 61, 5}, { 61, 6}, { 61, 7}, { 57, 4}, { 57, 5}, { 57, 6}, { 57, 7} },
      { { 60, 0}, { 60, 1}, { 60, 2}, { 60, 3}, { 56, 0}, { 56, 1}, { 56, 2}, { 56, 3}, { 60, 4}, { 60, 5}, { 60, 6}, { 60, 7}, { 56, 4}, { 56, 5}, { 56, 6}, { 56, 7} },
      { { 3, 8}, { 3, 9}, { 3, 10}, { 3, 11}, { 7, 8}, { 7, 9}, { 7, 10}, { 7, 11}, { 3, 12}, { 3, 13}, { 3, 14}, { 3, 15}, { 7, 12}, { 7, 13}, { 7, 14}, { 7, 15} },
      { { 2, 8}, { 2, 9}, { 2, 10}, { 2, 11}, { 6, 8}, { 6, 9}, { 6, 10}, { 6, 11}, { 2, 12}, { 2, 13}, { 2, 14}, { 2, 15}, { 6, 12}, { 6, 13}, { 6, 14}, { 6, 15} },
      { { 1, 8}, { 1, 9}, { 1, 10}, { 1, 11}, { 5, 8}, { 5, 9}, { 5, 10}, { 5, 11}, { 1, 12}, { 1, 13}, { 1, 14}, { 1, 15}, { 5, 12}, { 5, 13}, { 5, 14}, { 5, 15} },
      { { 0, 8}, { 0, 9}, { 0, 10}, { 0, 11}, { 4, 8}, { 4, 9}, { 4, 10}, { 4, 11}, { 0, 12}, { 0, 13}, { 0, 14}, { 0, 15}, { 4, 12}, { 4, 13}, { 4, 14}, { 4, 15} },
      { { 15, 8}, { 15, 9}, { 15, 10}, { 15, 11}, { 11, 8}, { 11, 9}, { 11, 10}, { 11, 11}, { 15, 12}, { 15, 13}, { 15, 14}, { 15, 15}, { 11, 12}, { 11, 13}, { 11, 14}, { 11, 15} },
      { { 14, 8}, { 14, 9}, { 14, 10}, { 14, 11}, { 10, 8}, { 10, 9}, { 10, 10}, { 10, 11}, { 14, 12}, { 14, 13}, { 14, 14}, { 14, 15}, { 10, 12}, { 10, 13}, { 10, 14}, { 10, 15} },
      { { 13, 8}, { 13, 9}, { 13, 10}, { 13, 11}, { 9, 8}, { 9, 9}, { 9, 10}, { 9, 11}, { 13, 12}, { 13, 13}, { 13, 14}, { 13, 15}, { 9, 12}, { 9, 13}, { 9, 14}, { 9, 15} },
      { { 12, 8}, { 12, 9}, { 12, 10}, { 12, 11}, { 8, 8}, { 8, 9}, { 8, 10}, { 8, 11}, { 12, 12}, { 12, 13}, { 12, 14}, { 12, 15}, { 8, 12}, { 8, 13}, { 8, 14}, { 8, 15} },
      { { 19, 8}, { 19, 9}, { 19, 10}, { 19, 11}, { 23, 8}, { 23, 9}, { 23, 10}, { 23, 11}, { 19, 12}, { 19, 13}, { 19, 14}, { 19, 15}, { 23, 12}, { 23, 13}, { 23, 14}, { 23, 15} },
      { { 18, 8}, { 18, 9}, { 18, 10}, { 18, 11}, { 22, 8}, { 22, 9}, { 22, 10}, { 22, 11}, { 18, 12}, { 18, 13}, { 18, 14}, { 18, 15}, { 22, 12}, { 22, 13}, { 22, 14}, { 22, 15} },
      { { 17, 8}, { 17, 9}, { 17, 10}, { 17, 11}, { 21, 8}, { 21, 9}, { 21, 10}, { 21, 11}, { 17, 12}, { 17, 13}, { 17, 14}, { 17, 15}, { 21, 12}, { 21, 13}, { 21, 14}, { 21, 15} },
      { { 16, 8}, { 16, 9}, { 16, 10}, { 16, 11}, { 20, 8}, { 20, 9}, { 20, 10}, { 20, 11}, { 16, 12}, { 16, 13}, { 16, 14}, { 16, 15}, { 20, 12}, { 20, 13}, { 20, 14}, { 20, 15} },
      { { 31, 8}, { 31, 9}, { 31, 10}, { 31, 11}, { 27, 8}, { 27, 9}, { 27, 10}, { 27, 11}, { 31, 12}, { 31, 13}, { 31, 14}, { 31, 15}, { 27, 12}, { 27, 13}, { 27, 14}, { 27, 15} },
      { { 30, 8}, { 30, 9}, { 30, 10}, { 30, 11}, { 26, 8}, { 26, 9}, { 26, 10}, { 26, 11}, { 30, 12}, { 30, 13}, { 30, 14}, { 30, 15}, { 26, 12}, { 26, 13}, { 26, 14}, { 26, 15} },
      { { 29, 8}, { 29, 9}, { 29, 10}, { 29, 11}, { 25, 8}, { 25, 9}, { 25, 10}, { 25, 11}, { 29, 12}, { 29, 13}, { 29, 14}, { 29, 15}, { 25, 12}, { 25, 13}, { 25, 14}, { 25, 15} },
      { { 28, 8}, { 28, 9}, { 28, 10}, { 28, 11}, { 24, 8}, { 24, 9}, { 24, 10}, { 24, 11}, { 28, 12}, { 28, 13}, { 28, 14}, { 28, 15}, { 24, 12}, { 24, 13}, { 24, 14}, { 24, 15} },
      { { 35, 8}, { 35, 9}, { 35, 10}, { 35, 11}, { 39, 8}, { 39, 9}, { 39, 10}, { 39, 11}, { 35, 12}, { 35, 13}, { 35, 14}, { 35, 15}, { 39, 12}, { 39, 13}, { 39, 14}, { 39, 15} },
      { { 34, 8}, { 34, 9}, { 34, 10}, { 34, 11}, { 38, 8}, { 38, 9}, { 38, 10}, { 38, 11}, { 34, 12}, { 34, 13}, { 34, 14}, { 34, 15}, { 38, 12}, { 38, 13}, { 38, 14}, { 38, 15} },
      { { 33, 8}, { 33, 9}, { 33, 10}, { 33, 11}, { 37, 8}, { 37, 9}, { 37, 10}, { 37, 11}, { 33, 12}, { 33, 13}, { 33, 14}, { 33, 15}, { 37, 12}, { 37, 13}, { 37, 14}, { 37, 15} },
      { { 32, 8}, { 32, 9}, { 32, 10}, { 32, 11}, { 36, 8}, { 36, 9}, { 36, 10}, { 36, 11}, { 32, 12}, { 32, 13}, { 32, 14}, { 32, 15}, { 36, 12}, { 36, 13}, { 36, 14}, { 36, 15} },
      { { 47, 8}, { 47, 9}, { 47, 10}, { 47, 11}, { 43, 8}, { 43, 9}, { 43, 10}, { 43, 11}, { 47, 12}, { 47, 13}, { 47, 14}, { 47, 15}, { 43, 12}, { 43, 13}, { 43, 14}, { 43, 15} },
      { { 46, 8}, { 46, 9}, { 46, 10}, { 46, 11}, { 42, 8}, { 42, 9}, { 42, 10}, { 42, 11}, { 46, 12}, { 46, 13}, { 46, 14}, { 46, 15}, { 42, 12}, { 42, 13}, { 42, 14}, { 42, 15} },
      { { 45, 8}, { 45, 9}, { 45, 10}, { 45, 11}, { 41, 8}, { 41, 9}, { 41, 10}, { 41, 11}, { 45, 12}, { 45, 13}, { 45, 14}, { 45, 15}, { 41, 12}, { 41, 13}, { 41, 14}, { 41, 15} },
      { { 44, 8}, { 44, 9}, { 44, 10}, { 44, 11}, { 40, 8}, { 40, 9}, { 40, 10}, { 40, 11}, { 44, 12}, { 44, 13}, { 44, 14}, { 44, 15}, { 40, 12}, { 40, 13}, { 40, 14}, { 40, 15} },
      { { 51, 8}, { 51, 9}, { 51, 10}, { 51, 11}, { 55, 8}, { 55, 9}, { 55, 10}, { 55, 11}, { 51, 12}, { 51, 13}, { 51, 14}, { 51, 15}, { 55, 12}, { 55, 13}, { 55, 14}, { 55, 15} },
      { { 50, 8}, { 50, 9}, { 50, 10}, { 50, 11}, { 54, 8}, { 54, 9}, { 54, 10}, { 54, 11}, { 50, 12}, { 50, 13}, { 50, 14}, { 50, 15}, { 54, 12}, { 54, 13}, { 54, 14}, { 54, 15} },
      { { 49, 8}, { 49, 9}, { 49, 10}, { 49, 11}, { 53, 8}, { 53, 9}, { 53, 10}, { 53, 11}, { 49, 12}, { 49, 13}, { 49, 14}, { 49, 15}, { 53, 12}, { 53, 13}, { 53, 14}, { 53, 15} },
      { { 48, 8}, { 48, 9}, { 48, 10}, { 48, 11}, { 52, 8}, { 52, 9}, { 52, 10}, { 52, 11}, { 48, 12}, { 48, 13}, { 48, 14}, { 48, 15}, { 52, 12}, { 52, 13}, { 52, 14}, { 52, 15} },
      { { 63, 8}, { 63, 9}, { 63, 10}, { 63, 11}, { 59, 8}, { 59, 9}, { 59, 10}, { 59, 11}, { 63, 12}, { 63, 13}, { 63, 14}, { 63, 15}, { 59, 12}, { 59, 13}, { 59, 14}, { 59, 15} },
      { { 62, 8}, { 62, 9}, { 62, 10}, { 62, 11}, { 58, 8}, { 58, 9}, { 58, 10}, { 58, 11}, { 62, 12}, { 62, 13}, { 62, 14}, { 62, 15}, { 58, 12}, { 58, 13}, { 58, 14}, { 58, 15} },
      { { 61, 8}, { 61, 9}, { 61, 10}, { 61, 11}, { 57, 8}, { 57, 9}, { 57, 10}, { 57, 11}, { 61, 12}, { 61, 13}, { 61, 14}, { 61, 15}, { 57, 12}, { 57, 13}, { 57, 14}, { 57, 15} },
      { { 60, 8}, { 60, 9}, { 60, 10}, { 60, 11}, { 56, 8}, { 56, 9}, { 56, 10}, { 56, 11}, { 60, 12}, { 60, 13}, { 60, 14}, { 60, 15}, { 56, 12}, { 56, 13}, { 56, 14}, { 56, 15} }
    };

    *matrix_x = mapping[x%64][y%16][0] + x - (x%64);
    *matrix_y = mapping[x%64][y%16][1] + y - (y%16);
  }
};

class StripeMultiplexMapper : public MultiplexMapperBase {
public:
  StripeMultiplexMapper() : MultiplexMapperBase("Stripe", 2) {}

  void MapSinglePanel(int x, int y, int *matrix_x, int *matrix_y) const {
    const bool is_top_stripe = (y % (panel_rows_/2)) < panel_rows_/4;
    *matrix_x = is_top_stripe ? x + panel_cols_ : x;
    *matrix_y = ((y / (panel_rows_/2)) * (panel_rows_/4)
                 + y % (panel_rows_/4));
  }
};

class CheckeredMultiplexMapper : public MultiplexMapperBase {
public:
  CheckeredMultiplexMapper() : MultiplexMapperBase("Checkered", 2) {}

  void MapSinglePanel(int x, int y, int *matrix_x, int *matrix_y) const {
    const bool is_top_check = (y % (panel_rows_/2)) < panel_rows_/4;
    const bool is_left_check = (x < panel_cols_/2);
    if (is_top_check) {
      *matrix_x = is_left_check ? x+panel_cols_/2 : x+panel_cols_;
    } else {
      *matrix_x = is_left_check ? x : x + panel_cols_/2;
    }
    *matrix_y = ((y / (panel_rows_/2)) * (panel_rows_/4)
                 + y % (panel_rows_/4));
  }
};

class SpiralMultiplexMapper : public MultiplexMapperBase {
public:
  SpiralMultiplexMapper() : MultiplexMapperBase("Spiral", 2) {}

  void MapSinglePanel(int x, int y, int *matrix_x, int *matrix_y) const {
    const bool is_top_stripe = (y % (panel_rows_/2)) < panel_rows_/4;
    const int panel_quarter = panel_cols_/4;
    const int quarter = x / panel_quarter;
    const int offset = x % panel_quarter;
    *matrix_x = ((2*quarter*panel_quarter)
                 + (is_top_stripe
                    ? panel_quarter - 1 - offset
                    : panel_quarter + offset));
    *matrix_y = ((y / (panel_rows_/2)) * (panel_rows_/4)
                 + y % (panel_rows_/4));
  }
};

class ZStripeMultiplexMapper : public MultiplexMapperBase {
public:
  ZStripeMultiplexMapper(const char *name, int even_vblock_offset, int odd_vblock_offset)
  : MultiplexMapperBase(name, 2),
    even_vblock_offset_(even_vblock_offset),
    odd_vblock_offset_(odd_vblock_offset) {}

  void MapSinglePanel(int x, int y, int *matrix_x, int *matrix_y) const {
    static const int tile_width = 8;
    static const int tile_height = 4;

    const int vert_block_is_odd = ((y / tile_height) % 2);

    const int even_vblock_shift = (1 - vert_block_is_odd) * even_vblock_offset_;
    const int odd_vblock_shitf = vert_block_is_odd * odd_vblock_offset_;

    *matrix_x = x + ((x + even_vblock_shift) / tile_width) * tile_width + odd_vblock_shitf;
    *matrix_y = (y % tile_height) + tile_height * (y / (tile_height * 2));
  }

private:
  const int even_vblock_offset_;
  const int odd_vblock_offset_;
};

class CoremanMapper : public MultiplexMapperBase {
public:
  CoremanMapper() : MultiplexMapperBase("coreman", 2) {}

  void MapSinglePanel(int x, int y, int *matrix_x, int *matrix_y) const {
    const bool is_left_check = (x < panel_cols_/2);

    if((y <= 7) || ((y >= 16) && (y <= 23))){
      *matrix_x = ((x / (panel_cols_/2)) * panel_cols_) + (x % (panel_cols_/2));
      if ((y & (panel_rows_/4)) == 0) {
        *matrix_y = (y / (panel_rows_/2)) * (panel_rows_/4) + (y % (panel_rows_/4));
      }
    } else {
      *matrix_x = is_left_check ? x + panel_cols_/2 : x + panel_cols_;
      *matrix_y = (y / (panel_rows_/2)) * (panel_rows_/4) + y % (panel_rows_/4);
    }
  }
};

/*
 * Here is where the registration happens.
 * If you add an instance of the mapper here, it will automatically be
 * made available in the --led-multiplexing commandline option.
 */
static MuxMapperList *CreateMultiplexMapperList() {
  MuxMapperList *result = new MuxMapperList();

  // Here, register all multiplex mappers from above.
  result->push_back(new StripeMultiplexMapper());
  result->push_back(new CheckeredMultiplexMapper());
  result->push_back(new SpiralMultiplexMapper());
  result->push_back(new ZStripeMultiplexMapper("ZStripe", 0, 8));
  result->push_back(new ZStripeMultiplexMapper("ZnMirrorZStripe", 4, 4));
  result->push_back(new CoremanMapper());
  result->push_back(new DoubleAbsenMultiplexMapper());
  return result;
}

const MuxMapperList &GetRegisteredMultiplexMappers() {
  static const MuxMapperList *all_mappers = CreateMultiplexMapperList();
  return *all_mappers;
}
}  // namespace internal
}  // namespace rgb_matrix

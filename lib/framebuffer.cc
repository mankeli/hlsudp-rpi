// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Copyright (C) 2013 Henner Zeller <h.zeller@acm.org>
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

// The framebuffer is the workhorse: it represents the frame in some internal
// format that is friendly to be dumped to the matrix quickly. Provides methods
// to manipulate the content.

#include "framebuffer-internal.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "gpio.h"

namespace rgb_matrix {
namespace internal {
enum {
  kBitPlanes = 11  // maximum usable bitplanes.
};

// We need one global instance of a timing correct pulser. There are different
// implementations depending on the context.
static PinPulser *sOutputEnablePulser = NULL;

#ifdef ONLY_SINGLE_SUB_PANEL
#  define SUB_PANELS_ 1
#else
#  define SUB_PANELS_ 2
#endif

PixelDesignator *PixelDesignatorMap::get(int x, int y) {
  if (x < 0 || y < 0 || x >= width_ || y >= height_)
    return NULL;
  return buffer_ + (y*width_) + x;
}

PixelDesignatorMap::PixelDesignatorMap(int width, int height)
  : width_(width), height_(height),
    buffer_(new PixelDesignator[width * height]) {
}

PixelDesignatorMap::~PixelDesignatorMap() {
  delete [] buffer_;
}

// Different panel types use different techniques to set the row address.
// We abstract that away with different implementations of RowAddressSetter
class RowAddressSetter {
public:
  virtual ~RowAddressSetter() {}
  virtual gpio_bits_t need_bits() const = 0;
  virtual void SetRowAddress(GPIO *io, int row) = 0;
};

namespace {

// The default DirectRowAddressSetter just sets the address in parallel
// output lines ABCDE with A the LSB and E the MSB.
class DirectRowAddressSetter : public RowAddressSetter {
public:
  DirectRowAddressSetter(int double_rows, const HardwareMapping &h)
    : row_mask_(0), last_row_(-1) {
    assert(double_rows <= 32);  // need to resize row_lookup_
    if (double_rows >= 32) row_mask_ |= h.e;
    if (double_rows >= 16) row_mask_ |= h.d;
    if (double_rows >=  8) row_mask_ |= h.c;
    if (double_rows >=  4) row_mask_ |= h.b;
    row_mask_ |= h.a;
    for (int i = 0; i < double_rows; ++i) {
      // To avoid the bit-fiddle in the critical path, utilize
      // a lookup-table for all possible rows.
      gpio_bits_t row_address = (i & 0x01) ? h.a : 0;
      row_address |= (i & 0x02) ? h.b : 0;
      row_address |= (i & 0x04) ? h.c : 0;
      row_address |= (i & 0x08) ? h.d : 0;
      row_address |= (i & 0x10) ? h.e : 0;
      row_lookup_[i] = row_address;
    }
  }

  virtual gpio_bits_t need_bits() const { return row_mask_; }

  virtual void SetRowAddress(GPIO *io, int row) {
    if (row == last_row_) return;
    io->WriteMaskedBits(row_lookup_[row], row_mask_);
    last_row_ = row;
  }

private:
  gpio_bits_t row_mask_;
  gpio_bits_t row_lookup_[32];
  int last_row_;
};

// This is mostly experimental at this point. It works with the one panel I have
// seen that does AB, but might need smallish tweaks to work with all panels
// that do this.
class ShiftRegisterRowAddressSetter : public RowAddressSetter {
public:
  ShiftRegisterRowAddressSetter(int double_rows, const HardwareMapping &h)
    : double_rows_(double_rows),
      row_mask_(h.a | h.b), clock_(h.a), data_(h.b),
      last_row_(-1) {
  }
  virtual gpio_bits_t need_bits() const { return row_mask_; }

  virtual void SetRowAddress(GPIO *io, int row) {
    if (row == last_row_) return;
    for (int activate = 0; activate < double_rows_; ++activate) {
      io->ClearBits(clock_);
      if (activate == double_rows_ - 1 - row) {
        io->ClearBits(data_);
      } else {
        io->SetBits(data_);
      }
      io->SetBits(clock_);
    }
    io->ClearBits(clock_);
    io->SetBits(clock_);
    last_row_ = row;
  }

private:
  const int double_rows_;
  const gpio_bits_t row_mask_;
  const gpio_bits_t clock_;
  const gpio_bits_t data_;
  int last_row_;
};

// The DirectABCDRowAddressSetter sets the address by one of
// row pin ABCD for 32х16 matrix 1:4 multiplexing. The matrix has
// 4 addressable rows. Row is selected by a low level on the
// corresponding row address pin. Other row address pins must be in high level.
//
// Row addr| 0 | 1 | 2 | 3
// --------+---+---+---+---
// Line A  | 0 | 1 | 1 | 1
// Line B  | 1 | 0 | 1 | 1
// Line C  | 1 | 1 | 0 | 1
// Line D  | 1 | 1 | 1 | 0
class DirectABCDLineRowAddressSetter : public RowAddressSetter {
public:
  DirectABCDLineRowAddressSetter(int double_rows, const HardwareMapping &h)
    : last_row_(-1) {
	row_mask_ = h.a | h.b | h.c | h.d;

	row_lines_[0] = /*h.a |*/ h.b | h.c | h.d;
	row_lines_[1] = h.a /*| h.b*/ | h.c | h.d;
	row_lines_[2] = h.a | h.b /*| h.c */| h.d;
	row_lines_[3] = h.a | h.b | h.c /*| h.d*/;
  }

  virtual gpio_bits_t need_bits() const { return row_mask_; }

  virtual void SetRowAddress(GPIO *io, int row) {
    if (row == last_row_) return;

    gpio_bits_t row_address = row_lines_[row % 4];

    io->WriteMaskedBits(row_address, row_mask_);
    last_row_ = row;
  }

private:
  gpio_bits_t row_lines_[4];
  gpio_bits_t row_mask_;
  int last_row_;
};

}

const struct HardwareMapping *Framebuffer::hardware_mapping_ = NULL;
RowAddressSetter *Framebuffer::row_setter_ = NULL;

Framebuffer::Framebuffer(int rows, int columns, int parallel,
                         int scan_mode,
                         const char *led_sequence, bool inverse_color,
                         PixelDesignatorMap **mapper)
  : rows_(rows),
    parallel_(parallel),
    height_(rows * parallel),
    columns_(columns),
    scan_mode_(scan_mode),
    led_sequence_(led_sequence), inverse_color_(inverse_color),
    pwm_bits_(kBitPlanes), do_luminance_correct_(true), brightness_(100),
    double_rows_(rows / SUB_PANELS_),
    buffer_size_(double_rows_ * columns_ * kBitPlanes * sizeof(gpio_bits_t)),
    shared_mapper_(mapper) {
  assert(hardware_mapping_ != NULL);   // Called InitHardwareMapping() ?
  assert(shared_mapper_ != NULL);  // Storage should be provided by RGBMatrix.
  assert(rows_ >=8 && rows_ <= 64 && rows_ % 2 == 0);
  if (parallel > hardware_mapping_->max_parallel_chains) {
    fprintf(stderr, "The %s GPIO mapping only supports %d parallel chain%s, "
            "but %d was requested.\n", hardware_mapping_->name,
            hardware_mapping_->max_parallel_chains,
            hardware_mapping_->max_parallel_chains > 1 ? "s" : "", parallel);
    abort();
  }
  assert(parallel >= 1 && parallel <= 3);

//  printf("new bitplane buffer: %i,%i\n",double_rows_, columns_);
//  bitplane_buffer_ = new gpio_bits_t[double_rows_ * columns_ * kBitPlanes];

  // THIS CODE WILL EXPLODE IF DISPLAY SIZE 
  bitplane_buffer_ = new gpio_bits_t[64 * columns_ * kBitPlanes];

  // If we're the first Framebuffer created, the shared PixelMapper is
  // still NULL, so create one.
  // The first PixelMapper represents the physical layout of a standard matrix
  // with the specific knowledge of the framebuffer, setting up PixelDesignators
  // in a way that they are useful for this Framebuffer.
  //
  // Newly created PixelMappers then can just copy around PixelDesignators
  // from the parent PixelMapper opaquely without having to know the details.

  printf("shared mapper: %p, %p\n", shared_mapper_, *shared_mapper_);
  if (*shared_mapper_ == NULL) {
    printf("creating default mapper %i, %i\n", columns_, height_);
    *shared_mapper_ = new PixelDesignatorMap(columns_, height_);
    for (int y = 0; y < height_; ++y) {
      for (int x = 0; x < columns_; ++x) {
        InitDefaultDesignator(x, y, (*shared_mapper_)->get(x, y));
      }
    }
  }

//  Clear();
}

Framebuffer::~Framebuffer() {
  delete [] bitplane_buffer_;
}

// TODO: this should also be parsed from some special formatted string, e.g.
// {addr={22,23,24,25,15},oe=18,clk=17,strobe=4, p0={11,27,7,8,9,10},...}
/* static */ void Framebuffer::InitHardwareMapping(const char *named_hardware) {
  if (named_hardware == NULL || *named_hardware == '\0') {
    named_hardware = "regular";
  }

  struct HardwareMapping *mapping = NULL;
  for (HardwareMapping *it = matrix_hardware_mappings; it->name; ++it) {
    if (strcasecmp(it->name, named_hardware) == 0) {
      mapping = it;
      break;
    }
  }

  if (!mapping) {
    fprintf(stderr, "There is no hardware mapping named '%s'.\nAvailable: ",
            named_hardware);
    for (HardwareMapping *it = matrix_hardware_mappings; it->name; ++it) {
      if (it != matrix_hardware_mappings) fprintf(stderr, ", ");
      fprintf(stderr, "'%s'", it->name);
    }
    fprintf(stderr, "\n");
    abort();
  }

  if (mapping->max_parallel_chains == 0) {
    // Auto determine.
    struct HardwareMapping *h = mapping;
    if ((h->p0_r1 | h->p0_g1 | h->p0_g1 | h->p0_r2 | h->p0_g2 | h->p0_g2) > 0)
      ++mapping->max_parallel_chains;
    if ((h->p1_r1 | h->p1_g1 | h->p1_g1 | h->p1_r2 | h->p1_g2 | h->p1_g2) > 0)
      ++mapping->max_parallel_chains;
    if ((h->p2_r1 | h->p2_g1 | h->p2_g1 | h->p2_r2 | h->p2_g2 | h->p2_g2) > 0)
      ++mapping->max_parallel_chains;
  }
  hardware_mapping_ = mapping;
}

/* static */ void Framebuffer::InitGPIO(GPIO *io, int rows, int parallel,
                                        bool allow_hardware_pulsing,
                                        int pwm_lsb_nanoseconds,
                                        int dither_bits,
                                        int row_address_type) {
  if (sOutputEnablePulser != NULL)
    return;  // already initialized.

  const struct HardwareMapping &h = *hardware_mapping_;
  // Tell GPIO about all bits we intend to use.
  gpio_bits_t all_used_bits = 0;

  all_used_bits |= h.output_enable | h.clock | h.strobe;

  all_used_bits |= h.p0_r1 | h.p0_g1 | h.p0_b1 | h.p0_r2 | h.p0_g2 | h.p0_b2;
  if (parallel >= 2) {
    all_used_bits |= h.p1_r1 | h.p1_g1 | h.p1_b1 | h.p1_r2 | h.p1_g2 | h.p1_b2;
  }
  if (parallel >= 3) {
    all_used_bits |= h.p2_r1 | h.p2_g1 | h.p2_b1 | h.p2_r2 | h.p2_g2 | h.p2_b2;
  }

  const int double_rows = rows / SUB_PANELS_;
  switch (row_address_type) {
  case 0:
    row_setter_ = new DirectRowAddressSetter(double_rows, h);
    break;
  case 1:
    row_setter_ = new ShiftRegisterRowAddressSetter(double_rows, h);
    break;
  case 2:
    row_setter_ = new DirectABCDLineRowAddressSetter(double_rows, h);
    break;
  default:
    assert(0);  // unexpected type.
  }

  all_used_bits |= row_setter_->need_bits();

  // Adafruit HAT identified by the same prefix.
  const bool is_some_adafruit_hat = (0 == strncmp(h.name, "adafruit-hat",
                                                  strlen("adafruit-hat")));
  // Initialize outputs, make sure that all of these are supported bits.
  const uint32_t result = io->InitOutputs(all_used_bits, is_some_adafruit_hat);
  assert(result == all_used_bits);  // Impl: all bits declared in gpio.cc ?

  std::vector<int> bitplane_timings;
  uint32_t timing_ns = pwm_lsb_nanoseconds;
  for (int b = 0; b < kBitPlanes; ++b) {
    bitplane_timings.push_back(timing_ns);
    if (b >= dither_bits) timing_ns *= 2;
  }
  sOutputEnablePulser = PinPulser::Create(io, h.output_enable,
                                          allow_hardware_pulsing,
                                          bitplane_timings);
}

bool Framebuffer::SetPWMBits(uint8_t value) {
  if (value < 1 || value > kBitPlanes)
    return false;
  pwm_bits_ = value;
  return true;
}

inline gpio_bits_t *Framebuffer::ValueAt(int double_row, int column, int bit) {
  return &bitplane_buffer_[ double_row * (columns_ * kBitPlanes)
                            + bit * columns_
                            + column ];
}

// Do CIE1931 luminance correction and scale to output bitplanes
static uint16_t luminance_cie1931(uint8_t c, uint8_t brightness) {
  float out_factor = 32.f*((1 << kBitPlanes) - 1);
  float v = (float) c * brightness / 255.0;
  return out_factor * ((v <= 8) ? v / 902.3 : pow((v + 16) / 116.0, 3));
}

struct ColorLookup {
  uint16_t color[256];
};
static ColorLookup *CreateLuminanceCIE1931LookupTable() {
  ColorLookup *for_brightness = new ColorLookup[100];
  for (int c = 0; c < 256; ++c)
    for (int b = 0; b < 100; ++b)
      for_brightness[b].color[c] = luminance_cie1931(c, b + 1);

  return for_brightness;
}

static inline uint16_t CIEMapColor(uint8_t brightness, uint8_t c) {
  static ColorLookup *luminance_lookup = CreateLuminanceCIE1931LookupTable();
  return luminance_lookup[brightness - 1].color[c];
}

// Non luminance correction. TODO: consider getting rid of this.
static inline uint16_t DirectMapColor(uint8_t brightness, uint8_t c) {
  // simple scale down the color value
  c = c * brightness / 100;

  enum {shift = kBitPlanes - 8};  //constexpr; shift to be left aligned.
  return (shift > 0) ? (c << shift) : (c >> -shift);
}

inline void Framebuffer::MapColors(
  uint8_t r, uint8_t g, uint8_t b,
  uint16_t *red, uint16_t *green, uint16_t *blue) {

  if (do_luminance_correct_) {
    *red   = CIEMapColor(brightness_, r);
    *green = CIEMapColor(brightness_, g);
    *blue  = CIEMapColor(brightness_, b);
  } else {
    *red   = DirectMapColor(brightness_, r);
    *green = DirectMapColor(brightness_, g);
    *blue  = DirectMapColor(brightness_, b);
  }

  if (inverse_color_) {
    *red = ~(*red);
    *green = ~(*green);
    *blue = ~(*blue);
  }
}

int Framebuffer::width() const { return (*shared_mapper_)->width(); }
int Framebuffer::height() const { return (*shared_mapper_)->height(); }

inline void Framebuffer::SetPixelHDR_tobp(int x, int y, uint16_t red, uint16_t green, uint16_t blue) {
  
  //static int n = 0;
  //n = (n+x+y+(rand()&3))&31;
int n = rand() & 31;
  //int n =16;
  //int n = (x*y) & 31;
  //int n = (rand() & 63)-16;


const int dither[8][8] = {
{ 0, 32, 8, 40, 2, 34, 10, 42}, /* 8x8 Bayer ordered dithering */
{48, 16, 56, 24, 50, 18, 58, 26}, /* pattern. Each input pixel */
{12, 44, 4, 36, 14, 46, 6, 38}, /* is scaled to the 0..63 range */
{60, 28, 52, 20, 62, 30, 54, 22}, /* before looking in this table */
{ 3, 35, 11, 43, 1, 33, 9, 41}, /* to determine the action. */
{51, 19, 59, 27, 49, 17, 57, 25},
{15, 47, 7, 39, 13, 45, 5, 37},
{63, 31, 55, 23, 61, 29, 53, 21} }; 

  //int n = (dither[x&7][y&7]) / 2;

//  n = (rand() & 15) | (n & (31^15));

  int redn = red + n;
  int greenn = green + n;
  int bluen = blue + n;

  #define clamp(_v, _min, _max) _v = _v > _max ? _max : (_v < _min ? _min : _v);
  clamp(redn, 0, 65535);
  clamp(greenn, 0, 65535);
  clamp(bluen, 0, 65535);

  red = redn / 32;
  green = greenn / 32;
  blue = bluen / 32;

  const PixelDesignator *designator = (*shared_mapper_)->get(x, y);
  if (designator == NULL) return;
  const int pos = designator->gpio_word;
  if (pos < 0) return;  // non-used pixel marker.

  uint32_t *bits = bitplane_buffer_ + pos;
  const int min_bit_plane = kBitPlanes - pwm_bits_;
  bits += (columns_ * min_bit_plane);
  const uint32_t r_bits = designator->r_bit;
  const uint32_t g_bits = designator->g_bit;
  const uint32_t b_bits = designator->b_bit;
  const uint32_t designator_mask = designator->mask;
  for (uint16_t mask = 1<<min_bit_plane; mask != 1<<kBitPlanes; mask <<=1 ) {
    uint32_t color_bits = 0;
    if (red & mask)   color_bits |= r_bits;
    if (green & mask) color_bits |= g_bits;
    if (blue & mask)  color_bits |= b_bits;
    *bits = (*bits & designator_mask) | color_bits;
    bits += columns_;
  }
}


// Strange LED-mappings such as RBG or so are handled here.
gpio_bits_t Framebuffer::GetGpioFromLedSequence(char col,
                                                gpio_bits_t default_r,
                                                gpio_bits_t default_g,
                                                gpio_bits_t default_b) {
  const char *pos = strchr(led_sequence_, col);
  if (pos == NULL) pos = strchr(led_sequence_, tolower(col));
  if (pos == NULL) {
    fprintf(stderr, "LED sequence '%s' does not contain any '%c'.\n",
            led_sequence_, col);
    abort();
  }
  switch (pos - led_sequence_) {
  case 0: return default_r;
  case 1: return default_g;
  case 2: return default_b;
  }
  return default_r;  // String too long, should've been caught earlier.
}

void Framebuffer::InitDefaultDesignator(int x, int y, PixelDesignator *d) {
  const struct HardwareMapping &h = *hardware_mapping_;
  uint32_t *bits = ValueAt(y % double_rows_, x, 0);
  d->gpio_word = bits - bitplane_buffer_;
  d->r_bit = d->g_bit = d->b_bit = 0;
  if (y < rows_) {
    if (y < double_rows_) {
      d->r_bit = GetGpioFromLedSequence('R', h.p0_r1, h.p0_g1, h.p0_b1);
      d->g_bit = GetGpioFromLedSequence('G', h.p0_r1, h.p0_g1, h.p0_b1);
      d->b_bit = GetGpioFromLedSequence('B', h.p0_r1, h.p0_g1, h.p0_b1);
    } else {
      d->r_bit = GetGpioFromLedSequence('R', h.p0_r2, h.p0_g2, h.p0_b2);
      d->g_bit = GetGpioFromLedSequence('G', h.p0_r2, h.p0_g2, h.p0_b2);
      d->b_bit = GetGpioFromLedSequence('B', h.p0_r2, h.p0_g2, h.p0_b2);
    }
  }
  else if (y >= rows_ && y < 2 * rows_) {
    if (y - rows_ < double_rows_) {
      d->r_bit = GetGpioFromLedSequence('R', h.p1_r1, h.p1_g1, h.p1_b1);
      d->g_bit = GetGpioFromLedSequence('G', h.p1_r1, h.p1_g1, h.p1_b1);
      d->b_bit = GetGpioFromLedSequence('B', h.p1_r1, h.p1_g1, h.p1_b1);
    } else {
      d->r_bit = GetGpioFromLedSequence('R', h.p1_r2, h.p1_g2, h.p1_b2);
      d->g_bit = GetGpioFromLedSequence('G', h.p1_r2, h.p1_g2, h.p1_b2);
      d->b_bit = GetGpioFromLedSequence('B', h.p1_r2, h.p1_g2, h.p1_b2);
    }
  }
  else {
    if (y - 2*rows_ < double_rows_) {
      d->r_bit = GetGpioFromLedSequence('R', h.p2_r1, h.p2_g1, h.p2_b1);
      d->g_bit = GetGpioFromLedSequence('G', h.p2_r1, h.p2_g1, h.p2_b1);
      d->b_bit = GetGpioFromLedSequence('B', h.p2_r1, h.p2_g1, h.p2_b1);
    } else {
      d->r_bit = GetGpioFromLedSequence('R', h.p2_r2, h.p2_g2, h.p2_b2);
      d->g_bit = GetGpioFromLedSequence('G', h.p2_r2, h.p2_g2, h.p2_b2);
      d->b_bit = GetGpioFromLedSequence('B', h.p2_r2, h.p2_g2, h.p2_b2);
    }
  }

  d->mask = ~(d->r_bit | d->g_bit | d->b_bit);
}

void Framebuffer::Serialize(const char **data, size_t *len) const {
  *data = reinterpret_cast<const char*>(bitplane_buffer_);
  *len = buffer_size_;
}

bool Framebuffer::Deserialize(const char *data, size_t len) {
  if (len != buffer_size_) return false;
  memcpy(bitplane_buffer_, data, len);
  return true;
}

void Framebuffer::CopyFrom(const Framebuffer *other) {
  if (other == this) return;
  memcpy(bitplane_buffer_, other->bitplane_buffer_, buffer_size_);
}

void Framebuffer::PrepareDump(
  uint16_t *color_r_,
  uint16_t *color_g_,
  uint16_t *color_b_,



  void** tileptrs_,
  int tileptrs_w_,
  int tileptrs_h_
) {

#if 0
  //printf("shared mapper %p\n", shared_mapper_);
  const PixelDesignator *designator = (*shared_mapper_)->get(0, 0);
  printf("shared map: %p, %p %p\n", shared_mapper_, *shared_mapper_, designator);

  if (designator)
  {
  const int pos = designator->gpio_word;
  const uint32_t r_bits = designator->r_bit;
  const uint32_t g_bits = designator->g_bit;
  const uint32_t b_bits = designator->b_bit;
  const uint32_t designator_mask = designator->mask;
    printf("%i (%i,%i,%i) %i\n", pos, r_bits, g_bits, b_bits, designator_mask);
  }
#endif

#if 1
  if (tileptrs_)
  {
    for (int ty = 0; ty < tileptrs_h_; ty++)
    {
#if 0
    static int robs = 0;
    static int robe = 0;
    robs++;
    robs %= 6;
    robe = robs+1;    
    for (int ty = robs; ty < robe; ty++)
    {
#endif
      for (int tx = 0; tx < tileptrs_w_; tx++)
      {
        uint16_t* tiledata = (uint16_t *)tileptrs_[ty * tileptrs_w_ + tx];
        if (tiledata)
        {
          for (int y = 0; y < 16; y++)
           for (int x = 0; x < 16; x++)
           {
              int offu = (y*16+x)*3;
              uint16_t r = tiledata[offu+0];
              uint16_t g = tiledata[offu+1];
              uint16_t b = tiledata[offu+2];
      
              SetPixelHDR_tobp(x+tx*16, y+ty*16, r, g, b);
              //SetPixelHDR_tobp(x, y, x*2, y*2, 0);
            }
        }
        else
        {

          for (int y = 0; y < 16; y++)
           for (int x = 0; x < 16; x++)
           {
             int offu = (y+ty*16)*columns_+(x+tx*16);

             SetPixelHDR_tobp((x+tx*16), (y+ty*16), color_r_[offu], color_g_[offu], color_b_[offu]);
           }

        }
      }
    }
  }
//  srand(666);
  else
  {
    static float off = 0;
    off+=0.01;
    for (int y = 0; y < height_; y++)
     for (int x = 0; x < columns_; x++)
     {
       int offu = y*columns_+x;

        SetPixelHDR_tobp(x, y, color_r_[offu], color_g_[offu], color_b_[offu]);
        //SetPixelHDR_tobp(x, y, x*2, 1000+sin(y*0.5f+off)*1000, 0);
      }
  }
#endif
}


void Framebuffer::DumpToMatrix(GPIO *io, int pwm_low_bit) {
  const struct HardwareMapping &h = *hardware_mapping_;
  gpio_bits_t color_clk_mask = 0;  // Mask of bits while clocking in.
  color_clk_mask |= h.p0_r1 | h.p0_g1 | h.p0_b1 | h.p0_r2 | h.p0_g2 | h.p0_b2;
  if (parallel_ >= 2) {
    color_clk_mask |= h.p1_r1 | h.p1_g1 | h.p1_b1 | h.p1_r2 | h.p1_g2 | h.p1_b2;
  }
  if (parallel_ >= 3) {
    color_clk_mask |= h.p2_r1 | h.p2_g1 | h.p2_b1 | h.p2_r2 | h.p2_g2 | h.p2_b2;
  }

  color_clk_mask |= h.clock;

  // Depending if we do dithering, we might not always show the lowest bits.
  const int start_bit = std::max(pwm_low_bit, kBitPlanes - pwm_bits_);

  const uint8_t half_double = double_rows_/2;
  for (uint8_t row_loop = 0; row_loop < double_rows_; ++row_loop) {
    uint8_t d_row;
    switch (scan_mode_) {
    case 0:  // progressive
    default:
      d_row = row_loop;
      break;

    case 1:  // interlaced
      d_row = ((row_loop < half_double)
               ? (row_loop << 1)
               : ((row_loop - half_double) << 1) + 1);
    }

    // Rows can't be switched very quickly without ghosting, so we do the
    // full PWM of one row before switching rows.
    for (int b = start_bit; b < kBitPlanes; ++b) {
      gpio_bits_t *row_data = ValueAt(d_row, 0, b);
      // While the output enable is still on, we can already clock in the next
      // data.
      for (int col = 0; col < columns_; ++col) {
        const gpio_bits_t &out = *row_data++;
        io->WriteMaskedBits(out, color_clk_mask);  // col + reset clock
        io->SetBits(h.clock);               // Rising edge: clock color in.
      }
      io->ClearBits(color_clk_mask);    // clock back to normal.

      // OE of the previous row-data must be finished before strobe.
      sOutputEnablePulser->WaitPulseFinished();

      // Setting address and strobing needs to happen in dark time.
      row_setter_->SetRowAddress(io, d_row);

      io->SetBits(h.strobe);   // Strobe in the previously clocked in row.
      io->ClearBits(h.strobe);

      // Now switch on for the sleep time necessary for that bit-plane.
      sOutputEnablePulser->SendPulse(b);
    }
  }
}



}  // namespace internal
}  // namespace rgb_matrix

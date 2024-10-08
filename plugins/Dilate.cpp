// Dilate.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

static const char* CLASS = "Dilate";

static const char* HELP =
  "Box Morphological Filter\n\n"

  "Maximum (or minimum) of a rectangular area around each pixel. This "
  "can be used to grow or shrink mattes.";

/*

   This code demonstrates how an image-buffer-based algorithim can be
   translated into Nuke's row-based multi-threading algorithim.

   If you can separate the horizontal and vertical passes, put them into
   different operators. Ideally put the vertical pass first, because this
   will put the cache it likely needs on the op's input, where it can be
   reused by other ops connected to that input.

   If you can separate your algorithim into such steps you can take advantage
   of the caching and multithreading of DDImage with no worries about managing
   locks.

 */

#include "DDImage/NukeWrapper.h"
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"
#include "DDImage/Tile.h"
#include "DDImage/DDMath.h"
#include <stdio.h>

using namespace DD::Image;

class Dilate : public Iop
{
  // Where the knob stores it's values:
  double w, h;

  // arguments for the horizontal pass:
  int h_size;
  int h_do_min;

  // arguments for the vertical pass:
  int v_size;
  int v_do_min;

public:

  Dilate(Node* node) : Iop(node)
  {
    w = h = 0;
  }

  void _validate(bool for_real) override
  {
    h_size = int(fabs(w) + .5);
    h_do_min = w < 0;
    v_size = int(fabs(h) + .5);
    v_do_min = h < 0;
    copy_info();
    info_.y(info_.y() - v_size);
    info_.t(info_.t() + v_size);
    info_.x(info_.x() - h_size);
    info_.r(info_.r() + h_size);
    set_out_channels(h_size || v_size ? Mask_All : Mask_None);
  }

  void _request(int x, int y, int r, int t, ChannelMask channels, int count) override
  {
    x -= h_size;
    r += h_size;
    y -= v_size;
    t += v_size;
    input0().request(x, y, r, t, channels, count);
  }

  // Find the minimum of all the input rows:
  void get_vpass(int y, int x, int r, ChannelMask channels, Row& out)
  {
    if (!v_size) {
      input0().get(y, x, r, channels, out);
      return;
    }
    // Get the input region we want to look at:
    Tile tile(input0(), x, y - v_size, r, y + v_size + 1, channels);

    // The constructor may abort, you can't look at tile then:
    if (aborted()) {
      return;
    }

    if (!tile.valid())  {
      // If the tile is invalid, the rows will not be allocated. However, the subsequent
      // program flow assumes the rows are present and filled.
      // Allocate the rows and fill them with black to avoid accessing NULL data later.

      foreach (z, channels) {
        float* TO = out.writable(z);
        memset(&TO[tile.x()], 0, (tile.r() - tile.x()) * sizeof(float));
      }

      return;
    }

    // Minimize all the lines into the output row:
    int left = tile.x();
    int right = tile.r();
    for (Channel z : channels) {
      float* TO = out.writable(z);
      int X;
      int Y = tile.y();
      Tile::LinePointers tlp = tile[z];
      Tile::RowPtr trp = tlp[Y];
      for (X = left; X < right; X++)
        TO[X] = trp[X];
      if (v_do_min) {
        for (Y++; Y < tile.t(); Y++) {
          trp = tlp[Y];
          for (X = left; X < right; X++)
            if (trp[X] < TO[X])
              TO[X] = trp[X];
        }
      }
      else {
        for (Y++; Y < tile.t(); Y++) {
          trp = tlp[Y];
          for (X = left; X < right; X++)
            if (trp[X] > TO[X])
              TO[X] = trp[X];
        }
      }
      // pad the ends that go outside the source:
      for (X = x; X < left; X++)
        TO[X] = TO[left];
      for (X = right; X < r; X++)
        TO[X] = TO[right - 1];
    }
  }

  // The engine does the horizontal minimum pass:
  void engine(int y, int x, int r, ChannelMask channels, Row& out) override
  {
    if (h_size) {
      Row in(x - h_size, r + h_size);
      get_vpass(y, x - h_size, r + h_size, channels, in);
      if (aborted())
        return;
      const int length = 2 * h_size;
      for (Channel z : channels) {
        const float* FROM = in[z];
        float* TO = out.writable(z);
        float v = FROM[x + h_size];
        if (h_do_min) {
          int X;
          for (X = x; X < r; X++) {
            if ((X - x) % length)
              v = std::min(v, FROM[X + h_size]);
            else
              v = FROM[X + h_size];
            TO[X] = v;
          }
          // we need to round up start to next multiple of length:
          X = (r - x) % length;
          if (X < 0)
            X = -X;
          X = X ? r + length - X : r;
          v = FROM[X - 1 - h_size];
          while (X > r) {
            --X;
            v = std::min(v, FROM[X - 1 - h_size]);
          }
          for (; X > x; X--) {
            if ((X - x) % length)
              v = std::min(v, FROM[X - 1 - h_size]);
            else
              v = FROM[X - 1 - h_size];
            TO[X - 1] = std::min(TO[X - 1], v);
          }
        }
        else {
          int X;
          for (X = x; X < r; X++) {
            if ((X - x) % length)
              v = std::max(v, FROM[X + h_size]);
            else
              v = FROM[X + h_size];
            TO[X] = v;
          }
          // we need to round up start to next multiple of length:
          X = (r - x) % length;
          if (X < 0)
            X = -X;
          X = X ? r + length - X : r;
          v = FROM[X - 1 - h_size];
          while (X > r) {
            --X;
            v = std::max(v, FROM[X - 1 - h_size]);
          }
          for (; X > x; X--) {
            if ((X - x) % length)
              v = std::max(v, FROM[X - 1 - h_size]);
            else
              v = FROM[X - 1 - h_size];
            TO[X - 1] = std::max(TO[X - 1], v);
          }
        }
      }
    }
    else {
      get_vpass(y, x, r, channels, out);
    }
  }

  const char* Class() const override { return CLASS; }
  const char* node_help() const override { return HELP; }

  void knobs(Knob_Callback f) override
  {
    WH_knob(f, &w, IRange(-100, 100), "size");
  }

  static const Op::Description d;

};

static Op* construct(Node* node) { return new NukeWrapper(new Dilate(node)); }
const Op::Description Dilate::d(CLASS, construct);

// end of Dilate.C

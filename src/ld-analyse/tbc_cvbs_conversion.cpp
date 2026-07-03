/*
 * File:        tbc_cvbs_conversion.cpp
 * Module:      ld-analyse
 * Purpose:     Convert legacy 16-bit ld-decode TBC samples to the 10-bit
 *              CVBS_U10_4FSC domain used by the scopes
 *
 * Ported from decode-orc's tbc_source {pal,ntsc,pal_m}_tbc_converter
 * (tbc_to_cvbs per-sample level mapping).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tbc_cvbs_conversion.h"

#include <cmath>

namespace tbc::cvbs {

int16_t tbc_to_cvbs(uint16_t tbc_sample, int32_t tbc_blanking,
                    int32_t tbc_white, ::VideoSystem system) {
  // Degenerate metadata: render the system 10-bit blanking baseline so the
  // scope still shows a sane axis rather than dividing by zero.
  const double range = static_cast<double>(tbc_white - tbc_blanking);
  if (range <= 0.0) {
    return static_cast<int16_t>(cvbs_blanking(system));
  }

  const double n =
      static_cast<double>(static_cast<int32_t>(tbc_sample) - tbc_blanking) /
      range;

  const int32_t sys_blank = cvbs_blanking(system);
  const int32_t sys_white = cvbs_white(system);
  const double cvbs =
      n * static_cast<double>(sys_white - sys_blank) + static_cast<double>(sys_blank);

  return static_cast<int16_t>(std::lround(cvbs));
}

}  // namespace tbc::cvbs

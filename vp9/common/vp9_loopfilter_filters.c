/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "vpx_config.h"
#include "vp9/common/vp9_common.h"
#include "vp9/common/vp9_loopfilter.h"
#include "vp9/common/vp9_onyxc_int.h"

static INLINE int8_t signed_char_clamp(int t) {
  return (int8_t)clamp(t, -128, 127);
}

// should we apply any filter at all: 11111111 yes, 00000000 no
static INLINE int8_t filter_mask(uint8_t limit, uint8_t blimit,
                                 uint8_t p3, uint8_t p2,
                                 uint8_t p1, uint8_t p0,
                                 uint8_t q0, uint8_t q1,
                                 uint8_t q2, uint8_t q3) {
  int8_t mask = 0;
  mask |= (abs(p3 - p2) > limit) * -1;
  mask |= (abs(p2 - p1) > limit) * -1;
  mask |= (abs(p1 - p0) > limit) * -1;
  mask |= (abs(q1 - q0) > limit) * -1;
  mask |= (abs(q2 - q1) > limit) * -1;
  mask |= (abs(q3 - q2) > limit) * -1;
  mask |= (abs(p0 - q0) * 2 + abs(p1 - q1) / 2  > blimit) * -1;
  return ~mask;
}

// is there high edge variance internal edge: 11111111 yes, 00000000 no
static INLINE int8_t hevmask(uint8_t thresh, uint8_t p1, uint8_t p0,
                             uint8_t q0, uint8_t q1) {
  int8_t hev = 0;
  hev  |= (abs(p1 - p0) > thresh) * -1;
  hev  |= (abs(q1 - q0) > thresh) * -1;
  return hev;
}

static INLINE void filter(int8_t mask, uint8_t hev, uint8_t *op1,
                          uint8_t *op0, uint8_t *oq0, uint8_t *oq1) {
  int8_t filter1, filter2;

  const int8_t ps1 = (int8_t) *op1 ^ 0x80;
  const int8_t ps0 = (int8_t) *op0 ^ 0x80;
  const int8_t qs0 = (int8_t) *oq0 ^ 0x80;
  const int8_t qs1 = (int8_t) *oq1 ^ 0x80;

  // add outer taps if we have high edge variance
  int8_t filter = signed_char_clamp(ps1 - qs1) & hev;

  // inner taps
  filter = signed_char_clamp(filter + 3 * (qs0 - ps0)) & mask;

  // save bottom 3 bits so that we round one side +4 and the other +3
  // if it equals 4 we'll set to adjust by -1 to account for the fact
  // we'd round 3 the other way
  filter1 = signed_char_clamp(filter + 4) >> 3;
  filter2 = signed_char_clamp(filter + 3) >> 3;

  *oq0 = signed_char_clamp(qs0 - filter1) ^ 0x80;
  *op0 = signed_char_clamp(ps0 + filter2) ^ 0x80;

  // outer tap adjustments
  filter = ((filter1 + 1) >> 1) & ~hev;

  *oq1 = signed_char_clamp(qs1 - filter) ^ 0x80;
  *op1 = signed_char_clamp(ps1 + filter) ^ 0x80;
}

void vp9_loop_filter_horizontal_edge_c(uint8_t *s, int p /* pitch */,
                                       const uint8_t *blimit,
                                       const uint8_t *limit,
                                       const uint8_t *thresh,
                                       int count) {
  int i;

  // loop filter designed to work using chars so that we can make maximum use
  // of 8 bit simd instructions.
  for (i = 0; i < 8 * count; ++i) {
    const uint8_t p3 = s[-4 * p], p2 = s[-3 * p], p1 = s[-2 * p], p0 = s[-p];
    const uint8_t q0 = s[0 * p],  q1 = s[1 * p],  q2 = s[2 * p],  q3 = s[3 * p];
    const int8_t mask = filter_mask(*limit, *blimit,
                                    p3, p2, p1, p0, q0, q1, q2, q3);
    const int8_t hev = hevmask(*thresh, p1, p0, q0, q1);
    filter(mask, hev, s - 2 * p, s - 1 * p, s, s + 1 * p);
    ++s;
  }
}

void vp9_loop_filter_vertical_edge_c(uint8_t *s, int pitch,
                                     const uint8_t *blimit,
                                     const uint8_t *limit,
                                     const uint8_t *thresh,
                                     int count) {
  int i;

  // loop filter designed to work using chars so that we can make maximum use
  // of 8 bit simd instructions.
  for (i = 0; i < 8 * count; ++i) {
    const uint8_t p3 = s[-4], p2 = s[-3], p1 = s[-2], p0 = s[-1];
    const uint8_t q0 = s[0],  q1 = s[1],  q2 = s[2],  q3 = s[3];
    const int8_t mask = filter_mask(*limit, *blimit,
                                    p3, p2, p1, p0, q0, q1, q2, q3);
    const int8_t hev = hevmask(*thresh, p1, p0, q0, q1);
    filter(mask, hev, s - 2, s - 1, s, s + 1);
    s += pitch;
  }
}

static INLINE int8_t flatmask4(uint8_t thresh,
                               uint8_t p3, uint8_t p2,
                               uint8_t p1, uint8_t p0,
                               uint8_t q0, uint8_t q1,
                               uint8_t q2, uint8_t q3) {
  int8_t flat = 0;
  flat |= (abs(p1 - p0) > thresh) * -1;
  flat |= (abs(q1 - q0) > thresh) * -1;
  flat |= (abs(p0 - p2) > thresh) * -1;
  flat |= (abs(q0 - q2) > thresh) * -1;
  flat |= (abs(p3 - p0) > thresh) * -1;
  flat |= (abs(q3 - q0) > thresh) * -1;
  return ~flat;
}
static INLINE signed char flatmask5(uint8_t thresh,
                                    uint8_t p4, uint8_t p3, uint8_t p2,
                                    uint8_t p1, uint8_t p0,
                                    uint8_t q0, uint8_t q1, uint8_t q2,
                                    uint8_t q3, uint8_t q4) {
  int8_t flat = 0;
  flat |= (abs(p4 - p0) > thresh) * -1;
  flat |= (abs(q4 - q0) > thresh) * -1;
  flat = ~flat;
  return flat & flatmask4(thresh, p3, p2, p1, p0, q0, q1, q2, q3);
}


static INLINE void mbfilter(int8_t mask, uint8_t hev, uint8_t flat,
                            uint8_t *op3, uint8_t *op2,
                            uint8_t *op1, uint8_t *op0,
                            uint8_t *oq0, uint8_t *oq1,
                            uint8_t *oq2, uint8_t *oq3) {
  // use a 7 tap filter [1, 1, 1, 2, 1, 1, 1] for flat line
  if (flat && mask) {
    const uint8_t p3 = *op3, p2 = *op2, p1 = *op1, p0 = *op0;
    const uint8_t q0 = *oq0, q1 = *oq1, q2 = *oq2, q3 = *oq3;

    *op2 = ROUND_POWER_OF_TWO(p3 + p3 + p3 + p2 + p2 + p1 + p0 + q0, 3);
    *op1 = ROUND_POWER_OF_TWO(p3 + p3 + p2 + p1 + p1 + p0 + q0 + q1, 3);
    *op0 = ROUND_POWER_OF_TWO(p3 + p2 + p1 + p0 + p0 + q0 + q1 + q2, 3);
    *oq0 = ROUND_POWER_OF_TWO(p2 + p1 + p0 + q0 + q0 + q1 + q2 + q3, 3);
    *oq1 = ROUND_POWER_OF_TWO(p1 + p0 + q0 + q1 + q1 + q2 + q3 + q3, 3);
    *oq2 = ROUND_POWER_OF_TWO(p0 + q0 + q1 + q2 + q2 + q3 + q3 + q3, 3);
  } else {
    filter(mask, hev, op1,  op0, oq0, oq1);
  }
}

void vp9_mbloop_filter_horizontal_edge_c(uint8_t *s, int p,
                                         const uint8_t *blimit,
                                         const uint8_t *limit,
                                         const uint8_t *thresh,
                                         int count) {
  int i;

  // loop filter designed to work using chars so that we can make maximum use
  // of 8 bit simd instructions.
  for (i = 0; i < 8 * count; ++i) {
    const uint8_t p3 = s[-4 * p], p2 = s[-3 * p], p1 = s[-2 * p], p0 = s[-p];
    const uint8_t q0 = s[0 * p], q1 = s[1 * p], q2 = s[2 * p], q3 = s[3 * p];

    const int8_t mask = filter_mask(*limit, *blimit,
                                    p3, p2, p1, p0, q0, q1, q2, q3);
    const int8_t hev = hevmask(*thresh, p1, p0, q0, q1);
    const int8_t flat = flatmask4(1, p3, p2, p1, p0, q0, q1, q2, q3);
    mbfilter(mask, hev, flat,
             s - 4 * p, s - 3 * p, s - 2 * p, s - 1 * p,
             s,         s + 1 * p, s + 2 * p, s + 3 * p);
    ++s;
  }
}

void vp9_mbloop_filter_vertical_edge_c(uint8_t *s, int pitch,
                                       const uint8_t *blimit,
                                       const uint8_t *limit,
                                       const uint8_t *thresh,
                                       int count) {
  int i;

  for (i = 0; i < 8 * count; ++i) {
    const uint8_t p3 = s[-4], p2 = s[-3], p1 = s[-2], p0 = s[-1];
    const uint8_t q0 = s[0], q1 = s[1], q2 = s[2], q3 = s[3];
    const int8_t mask = filter_mask(*limit, *blimit,
                                    p3, p2, p1, p0, q0, q1, q2, q3);
    const int8_t hev = hevmask(thresh[0], p1, p0, q0, q1);
    const int8_t flat = flatmask4(1, p3, p2, p1, p0, q0, q1, q2, q3);
    mbfilter(mask, hev, flat, s - 4, s - 3, s - 2, s - 1,
                              s,     s + 1, s + 2, s + 3);
    s += pitch;
  }
}

#if !CONFIG_NEW_LOOPFILTER
/* Vertical MB Filtering */
void vp9_loop_filter_mbv_c(uint8_t *y_ptr, uint8_t *u_ptr,
                           uint8_t *v_ptr, int y_stride, int uv_stride,
                           struct loop_filter_info *lfi) {
  vp9_mbloop_filter_vertical_edge_c(y_ptr, y_stride,
                                    lfi->mblim, lfi->lim, lfi->hev_thr, 2);

  if (u_ptr)
    vp9_mbloop_filter_vertical_edge_c(u_ptr, uv_stride,
                                      lfi->mblim, lfi->lim, lfi->hev_thr, 1);

  if (v_ptr)
    vp9_mbloop_filter_vertical_edge_c(v_ptr, uv_stride,
                                      lfi->mblim, lfi->lim, lfi->hev_thr, 1);
}

/* Vertical B Filtering */
void vp9_loop_filter_bv_c(uint8_t*y_ptr, uint8_t *u_ptr,
                          uint8_t *v_ptr, int y_stride, int uv_stride,
                          struct loop_filter_info *lfi) {
  vp9_loop_filter_vertical_edge_c(y_ptr + 4, y_stride,
                                  lfi->blim, lfi->lim, lfi->hev_thr, 2);
  vp9_loop_filter_vertical_edge_c(y_ptr + 8, y_stride,
                                  lfi->blim, lfi->lim, lfi->hev_thr, 2);
  vp9_loop_filter_vertical_edge_c(y_ptr + 12, y_stride,
                                  lfi->blim, lfi->lim, lfi->hev_thr, 2);

  if (u_ptr)
    vp9_loop_filter_vertical_edge_c(u_ptr + 4, uv_stride,
                                    lfi->blim, lfi->lim, lfi->hev_thr, 1);

  if (v_ptr)
    vp9_loop_filter_vertical_edge_c(v_ptr + 4, uv_stride,
                                    lfi->blim, lfi->lim, lfi->hev_thr, 1);
}

// Horizontal MB filtering
void vp9_loop_filter_mbh_c(uint8_t *y, uint8_t *u, uint8_t *v,
                           int y_stride, int uv_stride,
                           struct loop_filter_info *lfi) {
  vp9_mbloop_filter_horizontal_edge_c(y, y_stride,
                                      lfi->mblim, lfi->lim, lfi->hev_thr, 2);

  if (u)
    vp9_mbloop_filter_horizontal_edge_c(u, uv_stride,
                                        lfi->mblim, lfi->lim, lfi->hev_thr, 1);

  if (v)
    vp9_mbloop_filter_horizontal_edge_c(v, uv_stride,
                                        lfi->mblim, lfi->lim, lfi->hev_thr, 1);
}

// Horizontal B Filtering
void vp9_loop_filter_bh_c(uint8_t *y, uint8_t *u, uint8_t *v,
                          int y_stride, int uv_stride,
                          struct loop_filter_info *lfi) {
  vp9_loop_filter_horizontal_edge_c(y + 4 * y_stride, y_stride,
                                    lfi->blim, lfi->lim, lfi->hev_thr, 2);
  vp9_loop_filter_horizontal_edge_c(y + 8 * y_stride, y_stride,
                                    lfi->blim, lfi->lim, lfi->hev_thr, 2);
  vp9_loop_filter_horizontal_edge_c(y + 12 * y_stride, y_stride,
                                    lfi->blim, lfi->lim, lfi->hev_thr, 2);

  if (u)
    vp9_loop_filter_horizontal_edge_c(u + 4 * uv_stride, uv_stride,
                                      lfi->blim, lfi->lim, lfi->hev_thr, 1);

  if (v)
    vp9_loop_filter_horizontal_edge_c(v + 4 * uv_stride, uv_stride,
                                      lfi->blim, lfi->lim, lfi->hev_thr, 1);
}

void vp9_loop_filter_bh8x8_c(uint8_t *y, uint8_t *u, uint8_t *v,
                             int y_stride, int uv_stride,
                             struct loop_filter_info *lfi) {
  vp9_mbloop_filter_horizontal_edge_c(y + 8 * y_stride, y_stride,
                                      lfi->blim, lfi->lim, lfi->hev_thr, 2);

  if (u)
    vp9_loop_filter_horizontal_edge_c(u + 4 * uv_stride, uv_stride,
                                      lfi->blim, lfi->lim, lfi->hev_thr, 1);

  if (v)
    vp9_loop_filter_horizontal_edge_c(v + 4 * uv_stride, uv_stride,
                                      lfi->blim, lfi->lim, lfi->hev_thr, 1);
}


void vp9_loop_filter_bv8x8_c(uint8_t *y, uint8_t *u, uint8_t *v,
                             int y_stride, int uv_stride,
                             struct loop_filter_info *lfi) {
  vp9_mbloop_filter_vertical_edge_c(y + 8, y_stride,
                                    lfi->blim, lfi->lim, lfi->hev_thr, 2);

  if (u)
    vp9_loop_filter_vertical_edge_c(u + 4, uv_stride,
                                    lfi->blim, lfi->lim, lfi->hev_thr, 1);

  if (v)
    vp9_loop_filter_vertical_edge_c(v + 4, uv_stride,
                                    lfi->blim, lfi->lim, lfi->hev_thr, 1);
}
#endif

static INLINE void wide_mbfilter(int8_t mask, uint8_t hev,
                                 uint8_t flat, uint8_t flat2,
                                 uint8_t *op7, uint8_t *op6, uint8_t *op5,
                                 uint8_t *op4, uint8_t *op3, uint8_t *op2,
                                 uint8_t *op1, uint8_t *op0, uint8_t *oq0,
                                 uint8_t *oq1, uint8_t *oq2, uint8_t *oq3,
                                 uint8_t *oq4, uint8_t *oq5, uint8_t *oq6,
                                 uint8_t *oq7) {
  // use a 15 tap filter [1,1,1,1,1,1,1,2,1,1,1,1,1,1,1] for flat line
  if (flat2 && flat && mask) {
    const uint8_t p7 = *op7, p6 = *op6, p5 = *op5, p4 = *op4,
                  p3 = *op3, p2 = *op2, p1 = *op1, p0 = *op0;

    const uint8_t q0 = *oq0, q1 = *oq1, q2 = *oq2, q3 = *oq3,
                  q4 = *oq4, q5 = *oq5, q6 = *oq6, q7 = *oq7;

    *op6 = ROUND_POWER_OF_TWO(p7 * 7 + p6 * 2 + p5 + p4 + p3 + p2 + p1 + p0 +
                              q0, 4);
    *op5 = ROUND_POWER_OF_TWO(p7 * 6 + p6 + p5 * 2 + p4 + p3 + p2 + p1 + p0 +
                              q0 + q1, 4);
    *op4 = ROUND_POWER_OF_TWO(p7 * 5 + p6 + p5 + p4 * 2 + p3 + p2 + p1 + p0 +
                              q0 + q1 + q2, 4);
    *op3 = ROUND_POWER_OF_TWO(p7 * 4 + p6 + p5 + p4 + p3 * 2 + p2 + p1 + p0 +
                              q0 + q1 + q2 + q3, 4);
    *op2 = ROUND_POWER_OF_TWO(p7 * 3 + p6 + p5 + p4 + p3 + p2 * 2 + p1 + p0 +
                              q0 + q1 + q2 + q3 + q4, 4);
    *op1 = ROUND_POWER_OF_TWO(p7 * 2 + p6 + p5 + p4 + p3 + p2 + p1 * 2 + p0 +
                              q0 + q1 + q2 + q3 + q4 + q5, 4);
    *op0 = ROUND_POWER_OF_TWO(p7 + p6 + p5 + p4 + p3 + p2 + p1 + p0 * 2 +
                              q0 + q1 + q2 + q3 + q4 + q5 + q6, 4);
    *oq0 = ROUND_POWER_OF_TWO(p6 + p5 + p4 + p3 + p2 + p1 + p0 +
                              q0 * 2 + q1 + q2 + q3 + q4 + q5 + q6 + q7, 4);
    *oq1 = ROUND_POWER_OF_TWO(p5 + p4 + p3 + p2 + p1 + p0 +
                              q0 + q1 * 2 + q2 + q3 + q4 + q5 + q6 + q7 * 2, 4);
    *oq2 = ROUND_POWER_OF_TWO(p4 + p3 + p2 + p1 + p0 +
                              q0 + q1 + q2 * 2 + q3 + q4 + q5 + q6 + q7 * 3, 4);
    *oq3 = ROUND_POWER_OF_TWO(p3 + p2 + p1 + p0 +
                              q0 + q1 + q2 + q3 * 2 + q4 + q5 + q6 + q7 * 4, 4);
    *oq4 = ROUND_POWER_OF_TWO(p2 + p1 + p0 +
                              q0 + q1 + q2 + q3 + q4 * 2 + q5 + q6 + q7 * 5, 4);
    *oq5 = ROUND_POWER_OF_TWO(p1 + p0 +
                              q0 + q1 + q2 + q3 + q4 + q5 * 2 + q6 + q7 * 6, 4);
    *oq6 = ROUND_POWER_OF_TWO(p0 +
                              q0 + q1 + q2 + q3 + q4 + q5 + q6 * 2 + q7 * 7, 4);
  } else {
    mbfilter(mask, hev, flat, op3, op2, op1, op0, oq0, oq1, oq2, oq3);
  }
}

void vp9_mb_lpf_horizontal_edge_w(uint8_t *s, int p,
                                 const uint8_t *blimit,
                                 const uint8_t *limit,
                                 const uint8_t *thresh,
                                 int count) {
  int i;

  // loop filter designed to work using chars so that we can make maximum use
  // of 8 bit simd instructions.
  for (i = 0; i < 8 * count; ++i) {
    const uint8_t p3 = s[-4 * p], p2 = s[-3 * p], p1 = s[-2 * p], p0 = s[-p];
    const uint8_t q0 = s[0 * p], q1 = s[1 * p], q2 = s[2 * p], q3 = s[3 * p];
    const int8_t mask = filter_mask(*limit, *blimit,
                                    p3, p2, p1, p0, q0, q1, q2, q3);
    const int8_t hev = hevmask(*thresh, p1, p0, q0, q1);
    const int8_t flat = flatmask4(1, p3, p2, p1, p0, q0, q1, q2, q3);
    const int8_t flat2 = flatmask5(1,
                             s[-8 * p], s[-7 * p], s[-6 * p], s[-5 * p], p0,
                             q0, s[4 * p], s[5 * p], s[6 * p], s[7 * p]);

    wide_mbfilter(mask, hev, flat, flat2,
                  s - 8 * p, s - 7 * p, s - 6 * p, s - 5 * p,
                  s - 4 * p, s - 3 * p, s - 2 * p, s - 1 * p,
                  s,         s + 1 * p, s + 2 * p, s + 3 * p,
                  s + 4 * p, s + 5 * p, s + 6 * p, s + 7 * p);

    ++s;
  }
}

void vp9_mb_lpf_vertical_edge_w(uint8_t *s, int p,
                                const uint8_t *blimit,
                                const uint8_t *limit,
                                const uint8_t *thresh,
                                int count) {
  int i;

  for (i = 0; i < 8 * count; ++i) {
    const uint8_t p3 = s[-4], p2 = s[-3], p1 = s[-2], p0 = s[-1];
    const uint8_t q0 = s[0], q1 = s[1],  q2 = s[2], q3 = s[3];
    const int8_t mask = filter_mask(*limit, *blimit,
                                    p3, p2, p1, p0, q0, q1, q2, q3);
    const int8_t hev = hevmask(*thresh, p1, p0, q0, q1);
    const int8_t flat = flatmask4(1, p3, p2, p1, p0, q0, q1, q2, q3);
    const int8_t flat2 = flatmask5(1, s[-8], s[-7], s[-6], s[-5], p0,
                                   q0, s[4], s[5], s[6], s[7]);

    wide_mbfilter(mask, hev, flat, flat2,
                  s - 8, s - 7, s - 6, s - 5, s - 4, s - 3, s - 2, s - 1,
                  s,     s + 1, s + 2, s + 3, s + 4, s + 5, s + 6, s + 7);
    s += p;
  }
}

#if !CONFIG_NEW_LOOPFILTER
void vp9_lpf_mbv_w_c(uint8_t *y, uint8_t *u, uint8_t *v,
                     int y_stride, int uv_stride,
                     struct loop_filter_info *lfi) {
  vp9_mb_lpf_vertical_edge_w(y, y_stride,
                             lfi->mblim, lfi->lim, lfi->hev_thr, 2);

  if (u)
    vp9_mbloop_filter_vertical_edge_c(u, uv_stride,
                                      lfi->mblim, lfi->lim, lfi->hev_thr, 1);

  if (v)
    vp9_mbloop_filter_vertical_edge_c(v, uv_stride,
                                      lfi->mblim, lfi->lim, lfi->hev_thr, 1);
}

void vp9_lpf_mbh_w_c(uint8_t *y, uint8_t *u, uint8_t *v,
                     int y_stride, int uv_stride,
                     struct loop_filter_info *lfi) {
  vp9_mb_lpf_horizontal_edge_w(y, y_stride,
                               lfi->mblim, lfi->lim, lfi->hev_thr, 2);

  if (u)
    vp9_mbloop_filter_horizontal_edge_c(u, uv_stride,
                                        lfi->mblim, lfi->lim, lfi->hev_thr, 1);

  if (v)
    vp9_mbloop_filter_horizontal_edge_c(v, uv_stride,
                                        lfi->mblim, lfi->lim, lfi->hev_thr, 1);
}
#endif

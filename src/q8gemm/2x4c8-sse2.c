/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <immintrin.h>

#include <qnnpack/q8gemm.h>


static inline __m128i sse_reduce4_i32(__m128i x, __m128i y, __m128i z, __m128i w) {
#if defined(__SSSE3__) && !defined(__ANDROID__)
  /* xxyy = ( y2 + y3, y0 + y1, x2 + x3, x0 + x1 ) */
  const __m128i xxyy = _mm_hadd_epi32(x, y);
  /* zzww = ( w2 + w3, w0 + w1, z2 + z3, z0 + z1 ) */
  const __m128i zzww = _mm_hadd_epi32(z, w);
  /* xyzw = ( w0 + w1 + w2 + w3, y0 + y1 + y2 + y3, z0 + z1 + z2 + z3, x0 + x1 +
   * x2 + x3 ) */
  return _mm_hadd_epi32(xxyy, zzww);
#else
  /* xzxz = ( z1 + z3, x1 + x3, z0 + z2, x0 + x2 ) */
  const __m128i xzxz =
      _mm_add_epi32(_mm_unpacklo_epi32(x, z), _mm_unpackhi_epi32(x, z));
  /* ywyw = ( w1 + w3, y1 + y3, w0 + w2, y0 + y2 ) */
  const __m128i ywyw =
      _mm_add_epi32(_mm_unpacklo_epi32(y, w), _mm_unpackhi_epi32(y, w));
  /* xyzw = ( w0 + w2 + w1 + w3, y0 + y2 + y1 + y3, z0 + z2 + z1 + z3, x0 + x2 +
   * x1 + x3 ) */
  return _mm_add_epi32(
      _mm_unpacklo_epi32(xzxz, ywyw), _mm_unpackhi_epi32(xzxz, ywyw));
#endif
}

void q8gemm_ukernel_2x4c8__sse2(
    size_t mr,
    size_t nr,
    size_t k,
    const uint8_t* restrict a,
    size_t a_stride,
    const uint8_t* restrict b,
    const int32_t* restrict bias,
    uint8_t* restrict c,
    size_t c_stride,
    const uint8_t a_offset,
    const uint8_t b_offset,
    const union qnnp_q31_requantization_params requantization_params[restrict static 1])
{
  __m128i vacc00 = _mm_cvtsi32_si128((int)bias[0]);
  __m128i vacc01 = _mm_cvtsi32_si128((int)bias[1]);
  __m128i vacc02 = _mm_cvtsi32_si128((int)bias[2]);
  __m128i vacc03 = _mm_cvtsi32_si128((int)bias[3]);
  __m128i vacc10 = vacc00;
  __m128i vacc11 = vacc01;
  __m128i vacc12 = vacc02;
  __m128i vacc13 = vacc03;

  const uint8_t* a0 = a;
  const uint8_t* a1 = (const uint8_t*) ((uintptr_t) a0 + a_stride);
  if (mr != 2) {
    a1 = a0;
  }

  const uint8_t* b0 = b;
  const uint8_t* b1 = b0 + 8;
  if (nr < 2) {
    b1 = b0;
  }
  const uint8_t* b2 = b1 + 8;
  if (nr <= 2) {
    b2 = b1;
  }
  const uint8_t* b3 = b2 + 8;
  if (nr != 4) {
    b3 = b2;
  }
  const size_t b_stride = nr * 8;

  __m128i va_offset = _mm_set1_epi16((uint16_t) a_offset);
  const __m128i vb_offset = _mm_set1_epi16((uint16_t) b_offset);
  const __m128i vzero = _mm_setzero_si128();
  for (; k >= 8; k -= 8) {
    const __m128i va0 = _mm_sub_epi16(_mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*) a0), vzero), va_offset);
    a0 += 8;
    const __m128i va1 = _mm_sub_epi16(_mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*) a1), vzero), va_offset);
    a1 += 8;

    const __m128i vb0 = _mm_sub_epi16(_mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*) b0), vzero), vb_offset);
    b0 += b_stride;
    const __m128i vb1 = _mm_sub_epi16(_mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*) b1), vzero), vb_offset);
    b1 += b_stride;
    const __m128i vb2 = _mm_sub_epi16(_mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*) b2), vzero), vb_offset);
    b2 += b_stride;
    const __m128i vb3 = _mm_sub_epi16(_mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*) b3), vzero), vb_offset);
    b3 += b_stride;

    vacc00 = _mm_add_epi32(vacc00, _mm_madd_epi16(va0, vb0));
    vacc01 = _mm_add_epi32(vacc01, _mm_madd_epi16(va0, vb1));
    vacc02 = _mm_add_epi32(vacc02, _mm_madd_epi16(va0, vb2));
    vacc03 = _mm_add_epi32(vacc03, _mm_madd_epi16(va0, vb3));
    vacc10 = _mm_add_epi32(vacc10, _mm_madd_epi16(va1, vb0));
    vacc11 = _mm_add_epi32(vacc11, _mm_madd_epi16(va1, vb1));
    vacc12 = _mm_add_epi32(vacc12, _mm_madd_epi16(va1, vb2));
    vacc13 = _mm_add_epi32(vacc13, _mm_madd_epi16(va1, vb3));
  }
  if (k != 0) {
    const size_t a_predecrement = 8 - k;
    const __m128i va_shift = _mm_cvtsi32_si128(8 * a_predecrement);

    va_offset = _mm_unpacklo_epi8(
        _mm_srl_epi64(_mm_packus_epi16(va_offset, va_offset), va_shift), vzero);
    const __m128i va0 = _mm_sub_epi16(
        _mm_unpacklo_epi8(
            _mm_srl_epi64(
                _mm_loadl_epi64((const __m128i*)(a0 - a_predecrement)),
                va_shift),
            vzero),
        va_offset);
    const __m128i va1 = _mm_sub_epi16(
        _mm_unpacklo_epi8(
            _mm_srl_epi64(
                _mm_loadl_epi64((const __m128i*)(a1 - a_predecrement)),
                va_shift),
            vzero),
        va_offset);

    const __m128i vb0 = _mm_sub_epi16(
        _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*) b0), vzero),
        vb_offset);
    const __m128i vb1 = _mm_sub_epi16(
        _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*) b1), vzero),
        vb_offset);
    const __m128i vb2 = _mm_sub_epi16(
        _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*) b2), vzero),
        vb_offset);
    const __m128i vb3 = _mm_sub_epi16(
        _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*) b3), vzero),
        vb_offset);

    vacc00 = _mm_add_epi32(vacc00, _mm_madd_epi16(va0, vb0));
    vacc01 = _mm_add_epi32(vacc01, _mm_madd_epi16(va0, vb1));
    vacc02 = _mm_add_epi32(vacc02, _mm_madd_epi16(va0, vb2));
    vacc03 = _mm_add_epi32(vacc03, _mm_madd_epi16(va0, vb3));
    vacc10 = _mm_add_epi32(vacc10, _mm_madd_epi16(va1, vb0));
    vacc11 = _mm_add_epi32(vacc11, _mm_madd_epi16(va1, vb1));
    vacc12 = _mm_add_epi32(vacc12, _mm_madd_epi16(va1, vb2));
    vacc13 = _mm_add_epi32(vacc13, _mm_madd_epi16(va1, vb3));
  }

  __m128i vacc0x0123 = sse_reduce4_i32(vacc00, vacc01, vacc02, vacc03);
  __m128i vacc1x0123 = sse_reduce4_i32(vacc10, vacc11, vacc12, vacc13);

  const __m128i vmultiplier = _mm_load_si128((const __m128i*) requantization_params->sse2.multiplier);
  const __m128i vrounding = _mm_load_si128((const __m128i*) requantization_params->sse2.rounding);

  const __m128i vnmask0x0123 = _mm_cmpgt_epi32(_mm_setzero_si128(), vacc0x0123);
  const __m128i vnmask1x0123 = _mm_cmpgt_epi32(_mm_setzero_si128(), vacc1x0123);

  const __m128i vabsacc0x0123 = _mm_sub_epi32(_mm_xor_si128(vacc0x0123, vnmask0x0123), vnmask0x0123);
  const __m128i vabsacc1x0123 = _mm_sub_epi32(_mm_xor_si128(vacc1x0123, vnmask1x0123), vnmask1x0123);

  const __m128i vabsacc0x1032 = _mm_shuffle_epi32(vabsacc0x0123, _MM_SHUFFLE(2, 3, 0, 1));
  const __m128i vabsacc1x1032 = _mm_shuffle_epi32(vabsacc1x0123, _MM_SHUFFLE(2, 3, 0, 1));

  const __m128i vabsprod0x02 = _mm_mul_epu32(vabsacc0x0123, vmultiplier);
  const __m128i vabsprod1x02 = _mm_mul_epu32(vabsacc1x0123, vmultiplier);

  const __m128i vnmask0x02 = _mm_shuffle_epi32(vnmask0x0123, _MM_SHUFFLE(2, 2, 0, 0));
  const __m128i vnmask1x02 = _mm_shuffle_epi32(vnmask1x0123, _MM_SHUFFLE(2, 2, 0, 0));

  const __m128i vprod0x02 = _mm_sub_epi64(_mm_xor_si128(vabsprod0x02, vnmask0x02), vnmask0x02);
  const __m128i vprod1x02 = _mm_sub_epi64(_mm_xor_si128(vabsprod1x02, vnmask1x02), vnmask1x02);

  const __m128i vq31prod0x02 = _mm_srli_epi64(_mm_add_epi64(vprod0x02, vrounding), 31);
  const __m128i vq31prod1x02 = _mm_srli_epi64(_mm_add_epi64(vprod1x02, vrounding), 31);

  const __m128i vabsprod0x13 = _mm_mul_epu32(vabsacc0x1032, vmultiplier);
  const __m128i vabsprod1x13 = _mm_mul_epu32(vabsacc1x1032, vmultiplier);

  const __m128i vnmask0x13 = _mm_shuffle_epi32(vnmask0x0123, _MM_SHUFFLE(3, 3, 1, 1));
  const __m128i vnmask1x13 = _mm_shuffle_epi32(vnmask1x0123, _MM_SHUFFLE(3, 3, 1, 1));

  const __m128i vprod0x13 = _mm_sub_epi64(_mm_xor_si128(vabsprod0x13, vnmask0x13), vnmask0x13);
  const __m128i vprod1x13 = _mm_sub_epi64(_mm_xor_si128(vabsprod1x13, vnmask1x13), vnmask1x13);

  const __m128i vq31prod0x13 = _mm_srli_epi64(_mm_add_epi64(vprod0x13, vrounding), 31);
  const __m128i vq31prod1x13 = _mm_srli_epi64(_mm_add_epi64(vprod1x13, vrounding), 31);

  const __m128i vq31prod0x0213 = _mm_castps_si128(_mm_shuffle_ps(
      _mm_castsi128_ps(vq31prod0x02), _mm_castsi128_ps(vq31prod0x13), _MM_SHUFFLE(2, 0, 2, 0)));
  const __m128i vq31prod1x0213 = _mm_castps_si128(_mm_shuffle_ps(
      _mm_castsi128_ps(vq31prod1x02), _mm_castsi128_ps(vq31prod1x13), _MM_SHUFFLE(2, 0, 2, 0)));

  const __m128i vq31prod0x0123 = _mm_shuffle_epi32(vq31prod0x0213, _MM_SHUFFLE(3, 1, 2, 0));
  const __m128i vq31prod1x0123 = _mm_shuffle_epi32(vq31prod1x0213, _MM_SHUFFLE(3, 1, 2, 0));

  const __m128i vremainder_mask = _mm_load_si128((const __m128i*) requantization_params->sse2.remainder_mask);
  
  const __m128i vrem0x0123 =
    _mm_add_epi32(_mm_and_si128(vq31prod0x0123, vremainder_mask), _mm_cmpgt_epi32(_mm_setzero_si128(), vq31prod0x0123));
  const __m128i vrem1x0123 =
    _mm_add_epi32(_mm_and_si128(vq31prod1x0123, vremainder_mask), _mm_cmpgt_epi32(_mm_setzero_si128(), vq31prod1x0123));

  const __m128i vremainder_threshold = _mm_load_si128((const __m128i*) requantization_params->sse2.remainder_threshold);
  const __m128i vshift = _mm_load_si128((const __m128i*) requantization_params->sse2.shift);

  vacc0x0123 = 
    _mm_sub_epi32(_mm_sra_epi32(vq31prod0x0123, vshift), _mm_cmpgt_epi32(vrem0x0123, vremainder_threshold));
  vacc1x0123 = 
    _mm_sub_epi32(_mm_sra_epi32(vq31prod1x0123, vshift), _mm_cmpgt_epi32(vrem1x0123, vremainder_threshold));

  const __m128i vzero_point = _mm_load_si128((const __m128i*) requantization_params->sse2.zero_point);
  const __m128i vacc01x0123 = _mm_adds_epi16(_mm_packs_epi32(vacc0x0123, vacc1x0123), vzero_point);
  __m128i vout = _mm_packus_epi16(vacc01x0123, vacc01x0123);
  vout = _mm_max_epu8(vout, _mm_load_si128((const __m128i*) requantization_params->sse2.min));
  vout = _mm_min_epu8(vout, _mm_load_si128((const __m128i*) requantization_params->sse2.max));

  uint8_t* c0 = c;
  uint8_t* c1 = (uint8_t*) ((uintptr_t) c0 + c_stride);
  if (mr != 2) {
    c1 = c0;
  }
  if (nr == 4) {
    *((uint32_t*) c0) = (uint32_t) _mm_cvtsi128_si32(vout);
    *((uint32_t*) c1) = (uint32_t) _mm_cvtsi128_si32(_mm_srli_epi64(vout, 32));
  } else {
    if (nr >= 2) {
      *((uint16_t*) c0) = (uint16_t) _mm_extract_epi16(vout, 0);
      c0 += 2;
      *((uint16_t*) c1) = (uint16_t) _mm_extract_epi16(vout, 2);
      c1 += 2;
      vout = _mm_srli_epi32(vout, 16);
      nr -= 2;
    }
    if (nr != 0) {
      *((uint8_t*) c0) = (uint8_t) _mm_cvtsi128_si32(vout);
      *((uint8_t*) c1) = (uint8_t) _mm_extract_epi16(vout, 2);
    }
  }
}

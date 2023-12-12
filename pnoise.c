#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "mruby.h"
#include "mruby/class.h"
#include "mruby/data.h"
#include "mruby/istruct.h"
#include "mruby/presym/disable.h"
#include "mruby/string.h"
#include "mruby/value.h"
#include "mruby/variable.h"

/* taken from mruby at rev 0f45836b5954accf508f333f932741b925214471 */
/*  Written in 2019 by David Blackman and Sebastiano Vigna (vigna@acm.org)

To the extent possible under law, the author has dedicated all copyright
and related and neighboring rights to this software to the public domain
worldwide. This software is distributed without any warranty.

See <https://creativecommons.org/publicdomain/zero/1.0/>. */

#include <stdint.h>

/* This is xoshiro128++ 1.0, one of our 32-bit all-purpose, rock-solid
   generators. It has excellent speed, a state size (128 bits) that is
   large enough for mild parallelism, and it passes all tests we are aware
   of.

   For generating just single-precision (i.e., 32-bit) floating-point
   numbers, xoshiro128+ is even faster.

   The state must be seeded so that it is not everywhere zero. */


#ifdef MRB_32BIT
# define XORSHIFT96
# define NSEEDS 3
# define SEEDPOS 2
#else
# define NSEEDS 4
# define SEEDPOS 0
#endif
#define LASTSEED (NSEEDS-1)

typedef struct rand_state {
  uint32_t seed[NSEEDS];
} rand_state;

#ifndef XORSHIFT96
static inline uint32_t
rotl(const uint32_t x, int k) {
  return (x << k) | (x >> (32 - k));
}
#endif

static uint32_t
rand_uint32(rand_state *state)
{
#ifdef XORSHIFT96
  uint32_t *seed = state->seed;
  uint32_t x = seed[0];
  uint32_t y = seed[1];
  uint32_t z = seed[2];
  uint32_t t;

  t = (x ^ (x << 3)) ^ (y ^ (y >> 19)) ^ (z ^ (z << 6));
  x = y; y = z; z = t;
  seed[0] = x;
  seed[1] = y;
  seed[2] = z;

  return z;
#else
  uint32_t *s = state->seed;
  const uint32_t result = rotl(s[0] + s[3], 7) + s[0];
  const uint32_t t = s[1] << 9;

  s[2] ^= s[0];
  s[3] ^= s[1];
  s[1] ^= s[2];
  s[0] ^= s[3];

  s[2] ^= t;
  s[3] = rotl(s[3], 11);

  return result;
#endif  /* XORSHIFT96 */
  }

static void
random_check(mrb_state *mrb, mrb_value random) {
  struct RClass *c = mrb_class_get_id(mrb, MRB_SYM(Random));
  if (!mrb_obj_is_kind_of(mrb, random, c) || !mrb_istruct_p(random)) {
		mrb_p(mrb, mrb_bool_value(mrb_obj_class(mrb, random) == c));
    mrb_raise(mrb, E_TYPE_ERROR, "Random instance required");
  }
}

static mrb_value
random_default(mrb_state *mrb) {
  struct RClass *c = mrb_class_get(mrb, "Random");
  mrb_value d = mrb_const_get(mrb, mrb_obj_value(c), MRB_SYM(DEFAULT));
  if (!mrb_obj_is_kind_of(mrb, d, c)) {
    mrb_raise(mrb, E_TYPE_ERROR, "Random::DEFAULT replaced");
  }
  return d;
}

#define random_ptr(v) (rand_state*)mrb_istruct_ptr(v)
#define random_default_state(mrb) random_ptr(random_default(mrb))

/* end random */

#define PNOISE2D_SHARE_STATE

#define as_u64(exp) __builtin_bit_cast(uint64_t, (exp))

static const mrb_float empty_nan =
    __builtin_bit_cast(mrb_float, 0x7fffaaaaaaaaaaaa);

struct pnoise2d_state_t {
  mrb_float *data;
  uint32_t *ptbl;
  size_t w;
  size_t h;
#ifdef PNOISE2D_SHARE_STATE
  uint32_t refct;
#endif
  mrb_int octaves;
  mrb_float persistence;
  mrb_float lacunarity;
};

[[gnu::always_inline]] mrb_float lerp(mrb_float t, mrb_float a, mrb_float b) {
  return (b * t) + (a * (1 - t));
}

[[gnu::always_inline]] mrb_float fade(mrb_float t) {
  return (((t * 6 - 15) * t + 10) * t * t * t);
}

mrb_float grad2(uint8_t h, mrb_float x, mrb_float y) {
  switch (h & 0x7) {
  case 0:
    return +0 + y;
  case 1:
    return +x + y;
  case 2:
    return +x - 0;
  case 3:
    return +x - y;
  case 4:
    return +0 - y;
  case 5:
    return -x - y;
  case 6:
    return -x + 0;
  case 7:
    return -x + y;
  default:
    __builtin_unreachable();
  }
}

struct pnoise2d_state_t *pnoise2d_alloc(mrb_state *mrb, size_t w, size_t h) {
  struct pnoise2d_state_t *p =
      mrb_calloc(mrb, 1, sizeof(struct pnoise2d_state_t));

  mrb_float *data = mrb_malloc(mrb, w * h * sizeof(mrb_float));
  uint32_t *ptbl = mrb_calloc(mrb, (w > h ? w : h) * 2, sizeof(uint32_t));

  *p = (struct pnoise2d_state_t){
      .data = data,
      .ptbl = ptbl,
      .w = w,
      .h = h,
#ifdef PNOISE2D_SHARE_STATE
      .refct = 1,
#endif
      .octaves = 1,
      .persistence = 0.5,
      .lacunarity = 2.0,
  };
  return p;
}

void prepare_ptbl(uint32_t *ptbl, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    ptbl[i] = i;
  }
}

static void shuffle__uint32_t(rand_state *r, uint32_t *ary, size_t size) {
  for (size_t i = size - 1; i > 0; --i) {
    size_t j = rand_uint32(r) % (i + 1);

    uint8_t t = ary[i];
    ary[i] = ary[j];
    ary[j] = t;
  }
}

void memset_64(void *data, uint64_t v, size_t count) {
  uint64_t *data_ = (uint64_t *)data;
  for (size_t i = 0; i < count; ++i) {
    data_[i] = v;
  }
}

void pnoise2d_init(mrb_state *mrb, struct pnoise2d_state_t *p, mrb_int octaves,
                   mrb_float persistence, mrb_float lacunarity,
                   mrb_value rand) {
  p->octaves = octaves;
  p->persistence = persistence;
  p->lacunarity = lacunarity;

  rand_state *r = nullptr;

  if (!(mrb_nil_p(rand) || mrb_undef_p(rand))) {
    random_check(mrb, rand);
		r = random_ptr(rand);
	} else {
    r = random_default_state(mrb);
	}
		
  size_t ptbl_size = (p->w > p->h ? p->w : p->h) * 2;

  prepare_ptbl(p->ptbl, ptbl_size);
  shuffle__uint32_t(r, p->ptbl, ptbl_size);

  memset_64(p->data, empty_nan, p->w * p->h);
}

void pnoise2d_free(mrb_state *mrb, struct pnoise2d_state_t *p) {
  if (p == nullptr)
    return;
#ifdef PNOISE2D_SHARE_STATE
  if ((--p->refct) == 0) {
#endif
    mrb_free(mrb, p->ptbl);
    mrb_free(mrb, p->data);
    mrb_free(mrb, p);
#ifdef PNOISE2D_SHARE_STATE
  }
#endif
}

[[gnu::always_inline]] mrb_float clamp(mrb_float v, mrb_float a, mrb_float b) {
  if (v <= a)
    return a;
  if (v <= b)
    return v;
  return b;
}

mrb_float noise2d_cell_unchecked1(struct pnoise2d_state_t *p, size_t x,
                                  size_t y, mrb_int octave, mrb_float freq) {
  mrb_int period = 1 << octave;

  freq = freq / (mrb_float)period;
  mrb_float wfreq = p->w * freq;
  mrb_float hfreq = p->h * freq;

  mrb_float xa = fmod(x * freq, wfreq);
  mrb_float x1;
  mrb_float xf = modf(xa, &x1);
  x = x1;
  mrb_int x2 = fmod(x1 + 1.0, wfreq);
  mrb_float xb = fade(xf);

  mrb_float ya = fmod(y * freq, hfreq);
  mrb_float y1;
  mrb_float yf = modf(ya, &y1);
  y = y1;
  mrb_int y2 = fmod(y1 + 1.0, hfreq);
  mrb_float yb = fade(yf);

  uint32_t *ptbl = p->ptbl;

  uint32_t px1 = ptbl[x];
  uint32_t px2 = ptbl[x2];

  mrb_float top = lerp(xb, grad2(ptbl[px1 + y], xf, yf),
                       grad2(ptbl[px2 + y], xf - 1.0, yf));
  mrb_float bot = lerp(xb, grad2(ptbl[px1 + y2], xf, yf - 1.0),
                       grad2(ptbl[px2 + y2], xf - 1.0, yf - 1.0));
  return (lerp(yb, top, bot) + 1) / 2;
}

mrb_float noise2d_cell_unchecked(struct pnoise2d_state_t *p, size_t x,
                                 size_t y) {
  size_t idx = y * p->w + x;
  mrb_float *data = p->data;

  if (as_u64(data[idx]) != as_u64(empty_nan)) {
    return data[idx];
  }

  mrb_float sum = 0.0;
  mrb_float amp = 1.0;
  mrb_float freq = 0.1;

  mrb_int octaves = p->octaves;

  for (mrb_int octave = 0; octave < octaves; ++octave) {
    sum += noise2d_cell_unchecked1(p, x, y, octave, freq) * amp;
    amp *= p->persistence;
    freq *= p->lacunarity;
  }

  sum = clamp(sum, 0.0, 1.0);

  data[idx] = sum;
  return sum;
}

mrb_float noise2d_cell(struct pnoise2d_state_t *p, size_t x, size_t y) {
  if (x < 0 || x >= p->w || y < 0 || y >= p->h)
    return __builtin_nan("");
  return noise2d_cell_unchecked(p, x, y);
}

mrb_sym w_sym, h_sym, octaves_sym, persistence_sym, lacunarity_sym, rand_sym;

mrb_data_type pnoise2d_data_type = {
    .struct_name = "pnoise2d",
    .dfree = (void (*)(mrb_state *, void *))pnoise2d_free,
};

mrb_value pnoise2d_m_alloc(mrb_state *mrb, mrb_value klass) {
  mrb_raisef(mrb, E_TYPE_ERROR, "allocator undefined for %v", klass);
}

mrb_value pnoise2d_m_init(mrb_state *mrb, mrb_value self) {
  struct pnoise2d_state_t *p =
      mrb_data_check_get_ptr(mrb, self, &pnoise2d_data_type);

  if (p != nullptr)
    pnoise2d_free(mrb, p);

  const mrb_sym kws[] = {w_sym,           h_sym,          octaves_sym,
                         persistence_sym, lacunarity_sym, rand_sym};
  static const uint32_t numks = sizeof(kws) / sizeof(*kws);
  static const uint32_t reqks = 2;
  mrb_value kwvals[sizeof(kws) / sizeof(*kws)];
  const mrb_kwargs kwargs = {numks, reqks, kws, kwvals, NULL};

  mrb_get_args(mrb, ":", &kwargs);

  mrb_int w = mrb_integer(mrb_Integer(mrb, kwvals[0]));
  mrb_int h = mrb_integer(mrb_Integer(mrb, kwvals[1]));
  mrb_int octaves;
  mrb_float persistence;
  mrb_float lacunarity;
  mrb_value rand;

  if (mrb_undef_p(kwvals[2])) {
    octaves = 1;
  } else {
    octaves = mrb_integer(mrb_Integer(mrb, kwvals[2]));
  }

  if (mrb_undef_p(kwvals[3])) {
    persistence = 0.5;
  } else {
    persistence = mrb_float(mrb_Float(mrb, kwvals[3]));
  }

  if (mrb_undef_p(kwvals[4])) {
    lacunarity = 2;
  } else {
    lacunarity = mrb_float(mrb_Float(mrb, kwvals[4]));
  }

  if (mrb_undef_p(kwvals[5])) {
    rand = random_default(mrb);
  } else {
    rand = kwvals[5];
  }

  p = pnoise2d_alloc(mrb, w, h);
  pnoise2d_init(mrb, p, octaves, persistence, lacunarity, rand);

  DATA_PTR(self) = p;
  return mrb_nil_value();
}

mrb_value pnoise2d_cm_new(mrb_state *mrb, mrb_value klass) {
  const mrb_sym kws[] = {w_sym,           h_sym,          octaves_sym,
                         persistence_sym, lacunarity_sym, rand_sym};
  static const uint32_t numks = sizeof(kws) / sizeof(*kws);
  static const uint32_t reqks = 2;
  mrb_value kwvals[sizeof(kws) / sizeof(*kws)];
  const mrb_kwargs kwargs = {numks, reqks, kws, kwvals, NULL};

  mrb_get_args(mrb, ":", &kwargs);

  mrb_int w = mrb_integer(mrb_Integer(mrb, kwvals[0]));
  mrb_int h = mrb_integer(mrb_Integer(mrb, kwvals[1]));
  mrb_int octaves;
  mrb_float persistence;
  mrb_float lacunarity;
  mrb_value rand;

  if (mrb_undef_p(kwvals[2])) {
    octaves = 1;
  } else {
    octaves = mrb_integer(mrb_Integer(mrb, kwvals[2]));
  }

  if (mrb_undef_p(kwvals[3])) {
    persistence = 0.5;
  } else {
    persistence = mrb_float(mrb_Float(mrb, kwvals[3]));
  }

  if (mrb_undef_p(kwvals[4])) {
    lacunarity = 2.0;
  } else {
    lacunarity = mrb_float(mrb_Float(mrb, kwvals[4]));
  }

  if (mrb_undef_p(kwvals[5])) {
    rand = mrb_nil_value();
  } else {
    rand = kwvals[5];
  }

  struct pnoise2d_state_t *p = pnoise2d_alloc(mrb, w, h);
  pnoise2d_init(mrb, p, octaves, persistence, lacunarity, rand);

  mrb_value self = mrb_obj_value(mrb_data_object_alloc(
      mrb, mrb_class_ptr(klass), p, &pnoise2d_data_type));
  return self;
}

mrb_value pnoise2d_m_aref(mrb_state *mrb, mrb_value self) {
  struct pnoise2d_state_t *p =
      mrb_data_check_get_ptr(mrb, self, &pnoise2d_data_type);

  mrb_int x, y;
  mrb_get_args(mrb, "ii", &x, &y);

  return mrb_float_value(mrb, noise2d_cell(p, x, y));
}

void drb_register_c_extensions_with_api(mrb_state *mrb, struct drb_api_t *) {
  w_sym = mrb_intern_lit(mrb, "w");
  h_sym = mrb_intern_lit(mrb, "h");
  octaves_sym = mrb_intern_lit(mrb, "octaves");
  persistence_sym = mrb_intern_lit(mrb, "persistence");
  lacunarity_sym = mrb_intern_lit(mrb, "lacunarity");
  rand_sym = mrb_intern_lit(mrb, "rand");

  struct RClass *pnoise2d_klass =
      mrb_define_class(mrb, "PerlinNoise2D", mrb->object_class);
  mrb_define_class_method(mrb, pnoise2d_klass, "new", pnoise2d_cm_new,
                          MRB_ARGS_KEY(2, 5));
  mrb_define_method(mrb, pnoise2d_klass, "initialize", pnoise2d_m_init,
                    MRB_ARGS_KEY(2, 5));

  mrb_define_method(mrb, pnoise2d_klass, "[]", pnoise2d_m_aref,
                    MRB_ARGS_REQ(2));
}

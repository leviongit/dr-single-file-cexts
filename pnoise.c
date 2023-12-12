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

/* taken from mruby at rev f99e9963b2145812b6f2bd7f0dd3d8228c503c82 */

const struct mrb_data_type *rand_state_type;

typedef struct rand_state {
  uint32_t seed[4];
} rand_state;

static uint32_t rand_uint32(rand_state *state) {
  uint32_t *seed = state->seed;
  uint32_t x = seed[0];
  uint32_t y = seed[1];
  uint32_t z = seed[2];
  uint32_t w = seed[3];
  uint32_t t;

  t = x ^ (x << 11);
  x = y;
  y = z;
  z = w;
  w = (w ^ (w >> 19)) ^ (t ^ (t >> 8));
  seed[0] = x;
  seed[1] = y;
  seed[2] = z;
  seed[3] = w;

  return w;
}

static mrb_value random_default(mrb_state *mrb) {
  return mrb_const_get(mrb, mrb_obj_value(mrb_class_get(mrb, "Random")),
                       mrb_intern_lit(mrb, "DEFAULT"));
}

/* end random */

#define as_u64(exp) __builtin_bit_cast(uint64_t, (exp))

static const mrb_float empty_nan =
    __builtin_bit_cast(mrb_float, 0x7fffaaaaaaaaaaaa);

struct pnoise_state_t {
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
  mrb_float frequency;
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

struct pnoise_state_t *pnoise_alloc(mrb_state *mrb, size_t w, size_t h) {
  struct pnoise_state_t *p = mrb_calloc(mrb, 1, sizeof(struct pnoise_state_t));

  mrb_float *data = mrb_malloc(mrb, w * h * sizeof(mrb_float));
  uint32_t *ptbl = mrb_calloc(mrb, (w > h ? w : h) * 2, sizeof(uint32_t));

  *p = (struct pnoise_state_t){
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

void pnoise_init(mrb_state *mrb, struct pnoise_state_t *p, mrb_int octaves,
                 mrb_float persistence, mrb_float lacunarity,
                 mrb_float frequency, mrb_value rand) {
  p->octaves = octaves;
  p->persistence = persistence;
  p->lacunarity = lacunarity;
  p->frequency = frequency;

  rand_state *r = nullptr;

  if (!(mrb_nil_p(rand) || mrb_undef_p(rand))) {
    r = mrb_data_check_get_ptr(mrb, rand, rand_state_type);
  } else {
    r = mrb_data_check_get_ptr(mrb, random_default(mrb), rand_state_type);
  }

  size_t ptbl_size = (p->w > p->h ? p->w : p->h) * 2;

  prepare_ptbl(p->ptbl, ptbl_size);
  shuffle__uint32_t(r, p->ptbl, ptbl_size);

  memset_64(p->data, as_u64(empty_nan), p->w * p->h);
}

void pnoise_free(mrb_state *mrb, struct pnoise_state_t *p) {
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

mrb_float noise_cell_unchecked1(struct pnoise_state_t *p, size_t x, size_t y,
                                mrb_int octave, mrb_float freq) {
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

mrb_float noise_cell_unchecked(struct pnoise_state_t *p, size_t x, size_t y) {
  size_t idx = y * p->w + x;
  mrb_float *data = p->data;

  if (as_u64(data[idx]) != as_u64(empty_nan)) {
    return data[idx];
  }

  mrb_float sum = 0.0;
  mrb_float amp = 1.0;
  mrb_float freq = p->frequency;

  mrb_int octaves = p->octaves;

  for (mrb_int octave = 0; octave < octaves; ++octave) {
    sum += noise_cell_unchecked1(p, x, y, octave, freq) * amp;
    amp *= p->persistence;
    freq *= p->lacunarity;
  }

  sum = clamp(sum, 0.0, 1.0);

  data[idx] = sum;
  return sum;
}

mrb_float noise_cell(struct pnoise_state_t *p, size_t x, size_t y) {
  if (x < 0 || x >= p->w || y < 0 || y >= p->h)
    return __builtin_nan("");
  return noise_cell_unchecked(p, x, y);
}

mrb_sym width_sym, height_sym, octaves_sym, persistence_sym, lacunarity_sym,
    frequency_sym, rand_sym;

mrb_data_type pnoise_data_type = {
    .struct_name = "levi#pnoise",
    .dfree = (void (*)(mrb_state *, void *))pnoise_free,
};

mrb_value pnoise_m_alloc(mrb_state *mrb, mrb_value klass) {
  mrb_raisef(mrb, E_TYPE_ERROR, "allocator undefined for %v", klass);
}

mrb_value pnoise_m_init(mrb_state *mrb, mrb_value self) {
  struct pnoise_state_t *p =
      mrb_data_check_get_ptr(mrb, self, &pnoise_data_type);

  if (p != nullptr)
    pnoise_free(mrb, p);

  const mrb_sym kws[] = {width_sym,       height_sym,     octaves_sym,
                         persistence_sym, lacunarity_sym, frequency_sym,
                         rand_sym};
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
  mrb_float frequency;
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
    frequency = 1;
  } else {
    frequency = mrb_float(mrb_Float(mrb, kwvals[5]));
  }

  if (mrb_undef_p(kwvals[6])) {
    rand = random_default(mrb);
  } else {
    rand = kwvals[6];
  }

  p = pnoise_alloc(mrb, w, h);
  pnoise_init(mrb, p, octaves, persistence, lacunarity, frequency, rand);

  DATA_PTR(self) = p;
  return mrb_nil_value();
}

mrb_value pnoise_cm_new(mrb_state *mrb, mrb_value klass) {
  const mrb_sym kws[] = {width_sym,       height_sym,     octaves_sym,
                         persistence_sym, lacunarity_sym, frequency_sym,
                         rand_sym};
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
  mrb_float frequency;
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
    frequency = 0.1;
  } else {
    frequency = mrb_float(mrb_Float(mrb, kwvals[5]));
  }

  if (mrb_undef_p(kwvals[6])) {
    rand = random_default(mrb);
  } else {
    rand = kwvals[6];
  }

  struct pnoise_state_t *p = pnoise_alloc(mrb, w, h);
  pnoise_init(mrb, p, octaves, persistence, lacunarity, frequency, rand);

  mrb_value self = mrb_obj_value(
      mrb_data_object_alloc(mrb, mrb_class_ptr(klass), p, &pnoise_data_type));
  return self;
}

mrb_value pnoise_m_aref(mrb_state *mrb, mrb_value self) {
  struct pnoise_state_t *p =
      mrb_data_check_get_ptr(mrb, self, &pnoise_data_type);

  mrb_int x, y;
  mrb_get_args(mrb, "ii", &x, &y);

  return mrb_float_value(mrb, noise_cell(p, x, y));
}

void drb_register_c_extensions_with_api(mrb_state *mrb, void *) {
  width_sym = mrb_intern_lit(mrb, "width");
  height_sym = mrb_intern_lit(mrb, "height");
  octaves_sym = mrb_intern_lit(mrb, "octaves");
  persistence_sym = mrb_intern_lit(mrb, "persistence");
  lacunarity_sym = mrb_intern_lit(mrb, "lacunarity");
  frequency_sym = mrb_intern_lit(mrb, "frequency");
  rand_sym = mrb_intern_lit(mrb, "rand");

  rand_state_type = DATA_TYPE(random_default(mrb));

  struct RClass *noise_mod = mrb_define_module(mrb, "Noise");

  struct RClass *pnoise_klass =
      mrb_define_class_under(mrb, noise_mod, "PerlinNoise", mrb->object_class);

  mrb_define_class_method(mrb, pnoise_klass, "new", pnoise_cm_new,
                          MRB_ARGS_KEY(2, 5));
  mrb_define_method(mrb, pnoise_klass, "initialize", pnoise_m_init,
                    MRB_ARGS_KEY(2, 5));

  mrb_define_method(mrb, pnoise_klass, "[]", pnoise_m_aref, MRB_ARGS_REQ(2));
  mrb_define_method(mrb, pnoise_klass, "noise2d_value", pnoise_m_aref,
                    MRB_ARGS_REQ(2));
}

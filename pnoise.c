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

/* taken from mruby at rev 645a464b2ddc085a7dd026aa8ae9f810a8ad4934 */

/*  Written in 2019 by David Blackman and Sebastiano Vigna (vigna@acm.org)

To the extent possible under law, the author has dedicated all copyright
and related and neighboring rights to this software to the public domain
worldwide. This software is distributed without any warranty.

See <https://creativecommons.org/publicdomain/zero/1.0/>. */

/* This is xoshiro128++ 1.0, one of our 32-bit all-purpose, rock-solid
   generators. It has excellent speed, a state size (128 bits) that is
   large enough for mild parallelism, and it passes all tests we are aware
   of.

   For generating just single-precision (i.e., 32-bit) floating-point
   numbers, xoshiro128+ is even faster.

   The state must be seeded so that it is not everywhere zero. */

#ifdef MRB_32BIT
#define XORSHIFT96
#define NSEEDS 3
#define SEEDPOS 2
#else
#define NSEEDS 4
#define SEEDPOS 0
#endif
#define LASTSEED (NSEEDS - 1)

typedef struct rand_state {
  uint32_t seed[NSEEDS];
} rand_state;

#ifndef XORSHIFT96
static inline uint32_t rotl(const uint32_t x, int k) {
  return (x << k) | (x >> (32 - k));
}
#endif

static uint32_t rand_uint32(rand_state *state) {
#ifdef XORSHIFT96
  uint32_t *seed = state->seed;
  uint32_t x = seed[0];
  uint32_t y = seed[1];
  uint32_t z = seed[2];
  uint32_t t;

  t = (x ^ (x << 3)) ^ (y ^ (y >> 19)) ^ (z ^ (z << 6));
  x = y;
  y = z;
  z = t;
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
#endif /* XORSHIFT96 */
}

static mrb_int rand_i(rand_state *t, mrb_int max) {
  return rand_uint32(t) % max;
}

#define ID_RANDOM MRB_SYM(mruby_Random)

static mrb_value random_default(mrb_state *mrb) {
  struct RClass *c = mrb_class_get_id(mrb, ID_RANDOM);
  mrb_value d = mrb_iv_get(mrb, mrb_obj_value(c), ID_RANDOM);
  if (!mrb_obj_is_kind_of(mrb, d, c)) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "[BUG] default Random replaced");
  }
  return d;
}

#define random_ptr(v) (rand_state *)mrb_istruct_ptr(v)
#define random_default_state(mrb) random_ptr(random_default(mrb))

static rand_state *check_random_arg(mrb_state *mrb, mrb_value r) {
  struct RClass *c = mrb_class_get_id(mrb, ID_RANDOM);
  rand_state *random;

  if (mrb_undef_p(r)) {
    random = random_default_state(mrb);
  } else if (mrb_istruct_p(r) && mrb_obj_is_kind_of(mrb, r, c)) {
    random = (rand_state *)mrb_istruct_ptr(r);
  } else {
    mrb_raise(mrb, E_TYPE_ERROR, "Random object required");
  }
  return random;
}

/* end random */

#define PNOISE2D_SHARE_STATE

static const mrb_float empty_nan =
    __builtin_bit_cast(mrb_float, 0x7fffaaaaaaaaaaaa);

struct pnoise2d_state_t {
  mrb_float *data;
  uint8_t *ptbl;
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
  uint8_t *ptbl = mrb_calloc(mrb, (w > h ? w : h) * 2, sizeof(uint8_t));

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

[[gnu::always_inline]] void prepare_ptbl(uint8_t *ptbl, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    ptbl[i] = i & 0x7;
  }
}

static void shuffle__uint8_t(rand_state *r, uint8_t *ary, size_t size) {
  for (size_t i = size - 1; i > 0; --i) {
    size_t j = rand_i(r, i + 1);

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

  if (!mrb_nil_p(rand))
    r = check_random_arg(mrb, rand);
  else
    r = random_default_state(mrb);

  size_t ptbl_size = (p->w > p->h ? p->w : p->h) * 2;

  prepare_ptbl(p->ptbl, ptbl_size);
  shuffle__uint8_t(r, p->ptbl, ptbl_size);

	memset_64(p->data, empty_nan, p->w * p->h);
}

void pnoise2d_free(mrb_state *mrb, struct pnoise2d_state_t *p) {
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

mrb_data_type pnoise2d_data_type = {
    .struct_name = "pnoise2d",
    .dfree = (void (*)(mrb_state *, void *))pnoise2d_free,
};

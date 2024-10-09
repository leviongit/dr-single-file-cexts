#include <mruby.h>
#include <mruby/class.h>
#include <mruby/istruct.h>
#include <mruby/object.h>
#include <mruby/range.h>

#include <stdint.h>
#include <string.h>

/* xoroshiro128+ [https://prng.di.unimi.it/xoroshiro128plus.c] */
static inline uint64_t rotl(const uint64_t x, const uint8_t k) {
  return (x << k) | (x >> (64 - k));
}

struct xoroshiro128p_st {
  uint64_t lo;
  uint64_t hi;
};

static_assert(sizeof(struct xoroshiro128p_st) <= ISTRUCT_DATA_SIZE);

uint64_t xoroshiro128p_next(struct xoroshiro128p_st *st) {
  const uint64_t s0 = st->lo;
  uint64_t s1 = st->hi;

  const uint64_t res = rotl(s0 + s1, 17) + s0;

  s1 ^= s0;
  st->lo = rotl(s0, 49) ^ s1 ^ (s1 << 21);
  st->hi = rotl(s1, 28);

  return res;
}

void xoroshiro128p_jump(struct xoroshiro128p_st *st) {
  static const uint64_t JUMP[] = {0x2bd7a6a6e99c2ddc, 0x0992ccaf6a6fca05};

  uint64_t s0 = 0;
  uint64_t s1 = 0;
  for (uint64_t i = 0; i < (sizeof(JUMP) / sizeof(*JUMP)); i++)
    for (int b = 0; b < 64; b++) {
      if (JUMP[i] & UINT64_C(1) << b) {
        s0 ^= st->lo;
        s1 ^= st->hi;
      }
      xoroshiro128p_next(st);
    }

  st->lo = s0;
  st->hi = s1;
}

void xoroshiro128p_long_jump(struct xoroshiro128p_st *st) {
  static const uint64_t LONG_JUMP[] = {0x360fd5f2cf8d5d99, 0x9c6e6877736c46e3};

  uint64_t s0 = 0;
  uint64_t s1 = 0;
  for (uint64_t i = 0; i < (sizeof(LONG_JUMP) / sizeof(*LONG_JUMP)); i++)
    for (int b = 0; b < 64; b++) {
      if (LONG_JUMP[i] & UINT64_C(1) << b) {
        s0 ^= st->lo;
        s1 ^= st->hi;
      }
      xoroshiro128p_next(st);
    }

  st->lo = s0;
  st->hi = s1;
}

void xoroshiro128p_init(struct xoroshiro128p_st *st, uint64_t seed) {
  *st = (struct xoroshiro128p_st){
      .hi = rotl(seed ^ UINT64_C(0xfac1e04741dab55a), seed & 0x1f),
      .lo = rotl(seed, 12) ^ UINT64_C(0xf01e46382d57cab9)};
}

double xoroshiro128p_next_float(struct xoroshiro128p_st *st) {
  return (xoroshiro128p_next(st) & ~UINT64_C(1)) / (double)UINT64_MAX;
}

_Bool xoroshiro128p_next_bool(struct xoroshiro128p_st *st) {
  return xoroshiro128p_next(st) >> 63;
}

struct RClass *xoroshiro128p;

mrb_value xoro_rand_alloc(mrb_state *mrb, mrb_value) {
  struct RIStruct *ris =
      (struct RIStruct *)mrb_obj_alloc(mrb, MRB_TT_ISTRUCT, xoroshiro128p);
  mrb_value val = mrb_obj_value(ris);

  memset(ris->inline_data, 0, sizeof(ris->inline_data));

  return val;
}

mrb_value xoro_rand_new(mrb_state *mrb, mrb_value) {
  mrb_int seed = 0;
  mrb_get_args(mrb, "|i", &seed);

  struct RIStruct *ris =
      (struct RIStruct *)mrb_obj_alloc(mrb, MRB_TT_ISTRUCT, xoroshiro128p);
  mrb_value val = mrb_obj_value(ris);

  xoroshiro128p_init((struct xoroshiro128p_st *)&ris->inline_data, seed);

  return val;
}

mrb_value xoro_rand_init(mrb_state *mrb, mrb_value self) {
  if (!mrb_obj_is_instance_of(mrb, self, xoroshiro128p)) {
    mrb_raisef(mrb, E_TYPE_ERROR, "%C::initialize called on %T", xoroshiro128p,
               self);
  }

  mrb_int seed = 0;
  mrb_get_args(mrb, "|i", &seed);

  xoroshiro128p_init((struct xoroshiro128p_st *)ISTRUCT_PTR(self), seed);

  return self;
}

mrb_value xoro_rand_rand(mrb_state *mrb, mrb_value self) {
  mrb_value arg = mrb_int_value(mrb, 0);
  struct xoroshiro128p_st *st = (typeof(st))ISTRUCT_PTR(self);

  mrb_get_args(mrb, "|o", &arg);

  if (mrb_float_p(arg)) {
    arg = mrb_int_value(mrb, (int)mrb_float(arg));
  }

  if (mrb_integer_p(arg)) {
    mrb_int i = mrb_integer(arg);

    if (i < 0)
      mrb_raisef(mrb, E_ARGUMENT_ERROR, "negative number passed to %T#rand",
                 self);

    if (i == 0)
      return mrb_float_value(mrb, xoroshiro128p_next_float(st));

    return mrb_int_value(mrb, xoroshiro128p_next(st) % i);

    mrb_raise(mrb, E_RUNTIME_ERROR, "\"unreachable\" control flow");
  }

  if (mrb_range_p(arg)) {
    mrb_value a = mrb_range_beg(mrb, arg);
    mrb_value b = mrb_range_end(mrb, arg);
    _Bool a_int_p = mrb_integer_p(a);
    _Bool b_int_p = mrb_integer_p(b);
    _Bool a_flt_p = mrb_float_p(a);
    _Bool b_flt_p = mrb_float_p(b);

    if (!((a_int_p || a_flt_p) && (b_int_p || b_flt_p))) {
      mrb_raisef(mrb, E_ARGUMENT_ERROR, "range %v isn't a simple numeric range",
                 arg);
    }

    if (a_int_p && b_int_p) {
      mrb_int ia = mrb_integer(a);
      mrb_int ib = mrb_integer(b);
      if (ib < ia)
        return mrb_nil_value();

      mrb_int span = ib - ia + (mrb_int)!mrb_range_excl_p(mrb, arg);
      if (span == 0)
        return mrb_nil_value();

      mrb_int offs = xoroshiro128p_next(st) % span;
      return mrb_int_value(mrb, ia + offs);
    }

    mrb_float fa = a_int_p ? (mrb_float)mrb_integer(a) : mrb_float(a);
    mrb_float fb = a_int_p ? (mrb_float)mrb_integer(b) : mrb_float(b);
    if (fb < fa)
      return mrb_nil_value();

    mrb_float span = fb - fa;
    if (span == 0.0)
      return mrb_nil_value();

    return mrb_float_value(mrb, fa + (xoroshiro128p_next_float(st) * span));
  }

  mrb_raisef(mrb, E_ARGUMENT_ERROR, "%v is not a valid %T#rand argument", arg,
             self);
}

mrb_value xoro_rand_rand_bool([[maybe_unused]] mrb_state *mrb, mrb_value self) {
  return mrb_bool_value(
      xoroshiro128p_next_bool((struct xoroshiro128p_st *)ISTRUCT_PTR(self)));
}

mrb_value xoro_rand_jump_b([[maybe_unused]] mrb_state *mrb, mrb_value self) {
  xoroshiro128p_jump((struct xoroshiro128p_st *)ISTRUCT_PTR(self));
  return self;
}

mrb_value xoro_rand_jump(mrb_state *mrb, mrb_value self) {
  mrb_value newv = mrb_obj_dup(mrb, self);
  xoro_rand_jump_b(mrb, newv);
  return newv;
}

mrb_value xoro_rand_long_jump_b([[maybe_unused]] mrb_state *mrb,
                                mrb_value self) {
  xoroshiro128p_long_jump((struct xoroshiro128p_st *)ISTRUCT_PTR(self));
  return self;
}

mrb_value xoro_rand_long_jump(mrb_state *mrb, mrb_value self) {
  mrb_value newv = mrb_obj_dup(mrb, self);
  xoro_rand_long_jump_b(mrb, newv);
  return newv;
}

void drb_register_c_extensions_with_api(mrb_state *mrb, void *) {
  xoroshiro128p = mrb_define_class_id(mrb, mrb_intern_lit(mrb, "Xoroshiro128"),
                                      mrb->object_class);
  mrb_define_class_method_id(mrb, xoroshiro128p,
                             mrb_intern_lit(mrb, "allocate"), xoro_rand_alloc,
                             MRB_ARGS_NONE());
  mrb_define_class_method_id(mrb, xoroshiro128p, mrb_intern_lit(mrb, "new"),
                             xoro_rand_new, MRB_ARGS_OPT(1));
  mrb_define_method_id(mrb, xoroshiro128p, mrb_intern_lit(mrb, "initialize"),
                       xoro_rand_init, MRB_ARGS_OPT(1));

  mrb_define_method_id(mrb, xoroshiro128p, mrb_intern_lit(mrb, "rand"),
                       xoro_rand_rand, MRB_ARGS_OPT(1));
  mrb_define_method_id(mrb, xoroshiro128p, mrb_intern_lit(mrb, "rand_bool"),
                       xoro_rand_rand_bool, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, xoroshiro128p, mrb_intern_lit(mrb, "jump!"),
                       xoro_rand_jump_b, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, xoroshiro128p, mrb_intern_lit(mrb, "jump"),
                       xoro_rand_jump, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, xoroshiro128p, mrb_intern_lit(mrb, "long_jump!"),
                       xoro_rand_long_jump_b, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, xoroshiro128p, mrb_intern_lit(mrb, "long_jump"),
                       xoro_rand_long_jump, MRB_ARGS_NONE());

  MRB_SET_INSTANCE_TT(xoroshiro128p, MRB_TT_ISTRUCT);
}

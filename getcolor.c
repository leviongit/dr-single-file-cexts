#include "dragonruby.h"
#include <inttypes.h>
#include <stdint.h>

#define typeof_field(STRUCT, FIELD) typeof(((STRUCT *)0)->FIELD)

#define usym(name) usym__##name
#define usym_val(name) usym(val__##name)

#define sym_gd(name)                                                           \
  mrb_sym usym(name);                                                          \
  mrb_value usym_val(name)

sym_gd(r);
sym_gd(g);
sym_gd(b);
sym_gd(a);

#undef sym_gd

struct drb_api_t *drb;

struct imgdata_t {
  union {
    uint8_t array[4];
    uint32_t abgr;
    struct {
      uint8_t r;
      uint8_t g;
      uint8_t b;
      uint8_t a;
    };
  } *as;
  uint32_t w;
  uint32_t h;
};

static_assert(sizeof(*(typeof_field(struct imgdata_t, as))0) == 4);

struct imgdata_t imgdata_cons(const char *fname) {
  int w;
  int h;
  void *imgdata = drb->drb_load_image(fname, &w, &h);
  return (struct imgdata_t){
      .as = (typeof_field(struct imgdata_t, as))imgdata, .w = w, .h = h};
}

void imgdata_dtor(mrb_state *_, struct imgdata_t *ptr) {
  drb->drb_free_image((void *)ptr->as);
}

mrb_value getcolor_getpixel_ncache(mrb_state *mrb, mrb_value _) {
  char *fpath;
  mrb_int x;
  mrb_int y;
  mrb_get_args(mrb, "zii", &fpath, &x, &y);

  struct imgdata_t imgdata = imgdata_cons(fpath);

  if (!imgdata.as) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "imgdata of %s doesn't exist", fpath);
  }

  if (x < 0 || x >= imgdata.w || y < 0 || y >= imgdata.h) {
    imgdata_dtor(mrb, &imgdata); // this only invalidates imgdata::as
    mrb_raisef(mrb, E_ARGUMENT_ERROR,
               "pixel out of bounds: tried to get pixel at (%i:%i) of texture "
               "%s with size (%i:%i)",
               x, y, fpath, imgdata.w - 1, imgdata.h - 1);
  }

  typeof(*(typeof_field(struct imgdata_t, as))0) color =
      imgdata.as[(imgdata.h - y - 1) * imgdata.w + x + 1];

  imgdata_dtor(mrb, &imgdata);

  mrb_value vhash = mrb_hash_new_capa(mrb, 4);

  mrb_hash_set(mrb, vhash, usym_val(r), mrb_int_value(mrb, color.r));
  mrb_hash_set(mrb, vhash, usym_val(g), mrb_int_value(mrb, color.g));
  mrb_hash_set(mrb, vhash, usym_val(b), mrb_int_value(mrb, color.b));
  mrb_hash_set(mrb, vhash, usym_val(a), mrb_int_value(mrb, color.a));

  return vhash;
}

#define init_sym(mrb, name)                                                    \
  usym(name) = mrb_intern_lit(mrb, #name);                                     \
  usym_val(name) = mrb_symbol_value(usym(name))

void drb_register_c_extensions_with_api(mrb_state *mrb, struct drb_api_t *api) {
  drb = api;

  init_sym(mrb, r);
  init_sym(mrb, g);
  init_sym(mrb, b);
  init_sym(mrb, a);

  struct RClass *ColorPicker =
    mrb_define_module_id(mrb, mrb_intern_lit(mrb, "ColorPicker"));

  mrb_define_module_function_id(mrb, ColorPicker, mrb_intern_lit(mrb, "pixel"), getcolor_getpixel_ncache, MRB_ARGS_REQ(3));
}

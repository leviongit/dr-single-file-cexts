#include <mruby.h>
#include <mruby/hash.h>
#include <mruby/variable.h>

typedef struct Box {
  mrb_float x;
  mrb_float y;
  mrb_float w;
  mrb_float h;
	mrb_float anchor_x;
	mrb_float anchor_y;
} Box;

mrb_sym sym$x;
mrb_value val$sym$x;
mrb_sym sym$y;
mrb_value val$sym$y;
mrb_sym sym$w;
mrb_value val$sym$w;
mrb_sym sym$h;
mrb_value val$sym$h;
mrb_sym sym$anchor_x;
mrb_value val$sym$anchor_x;
mrb_sym sym$anchor_y;
mrb_value val$sym$anchor_y;

mrb_float qtr_extract_flt_property(mrb_state *mrb, mrb_value obj,
                                   mrb_value key) {
  if (mrb_hash_p(obj)) {
    mrb_value property = mrb_hash_get(mrb, obj, key);
    if (mrb_float_p(property)) {
      return mrb_float(property);
    } else if (mrb_integer_p(property)) {
      return (mrb_float)mrb_integer(property);
    } else if (mrb_nil_p(property)) {
      return 0.0;
    } else {
      mrb_raisef(mrb, E_TYPE_ERROR,
                 "non-float value on hash key %v for hash %v", key, obj);
    }
  } else {
    mrb_raisef(mrb, E_TYPE_ERROR,
               "tried to get float property from a non-hash");
  }
}

Box qtr_box_of_hash(mrb_state *mrb, mrb_value hash) {
  mrb_float x = qtr_extract_flt_property(mrb, hash, val$sym$x);
  mrb_float y = qtr_extract_flt_property(mrb, hash, val$sym$y);
  mrb_float w = qtr_extract_flt_property(mrb, hash, val$sym$w);
  mrb_float h = qtr_extract_flt_property(mrb, hash, val$sym$h);
  mrb_float anchor_x = qtr_extract_flt_property(mrb, hash, val$sym$anchor_x);
  mrb_float anchor_y = qtr_extract_flt_property(mrb, hash, val$sym$anchor_y);

	return (Box) {
    .x = x - anchor_x * w,
		.y = y - anchor_y * h,
		.w = w,
		.h = h,
		.anchor_x = anchor_x,
		.anchor_y = anchor_y
	};
}

mrb_value qtr_normalize_hash_b(mrb_state *mrb, mrb_value hash) {
  mrb_check_frozen(mrb, mrb_hash_ptr(hash));

	Box box = qtr_box_of_hash(mrb, hash);

  mrb_hash_set(mrb, hash, val$sym$x, mrb_float_value(mrb, box.x));
  mrb_hash_set(mrb, hash, val$sym$y, mrb_float_value(mrb, box.y));
  mrb_hash_delete_key(mrb, hash, val$sym$anchor_x);
  mrb_hash_delete_key(mrb, hash, val$sym$anchor_y);

  return hash;
}

mrb_value qtr_r_normalize_hash_b(mrb_state *mrb, mrb_value self) {
	return qtr_normalize_hash_b(mrb, self);
}

mrb_value qtr_r_normalize_hash(mrb_state *mrb, mrb_value self) {
  mrb_value new = mrb_hash_dup(mrb, self);

  qtr_r_normalize_hash_b(mrb, new);

  return new;
}

mrb_value qtr_scale_hash_b(mrb_state *mrb, mrb_value hash, mrb_float scale) {
	mrb_float w = qtr_extract_flt_property(mrb, hash, val$sym$w);
	mrb_float h = qtr_extract_flt_property(mrb, hash, val$sym$h);

	mrb_hash_set(mrb, hash, val$sym$w, mrb_float_value(mrb, w * scale));
	mrb_hash_set(mrb, hash, val$sym$h, mrb_float_value(mrb, h * scale));

	return hash;
}

mrb_value qtr_r_scale_hash_b(mrb_state *mrb, mrb_value self) {
	mrb_float scale;
	mrb_get_args(mrb, "f", &scale);

	return qtr_scale_hash_b(mrb, self, scale);
}

mrb_value qtr_r_scale_hash(mrb_state *mrb, mrb_value self) {
	mrb_float scale;
	mrb_get_args(mrb, "f", &scale);
	
	mrb_value new = mrb_hash_dup(mrb, self);
	
	qtr_scale_hash_b(mrb, new, scale);

	return new;
}

void drb_register_c_extensions_with_api(mrb_state *mrb, void *) {
  sym$x = mrb_intern_lit(mrb, "x");
  val$sym$x = mrb_symbol_value(sym$x);
  sym$y = mrb_intern_lit(mrb, "y");
  val$sym$y = mrb_symbol_value(sym$y);
  sym$w = mrb_intern_lit(mrb, "w");
  val$sym$w = mrb_symbol_value(sym$w);
  sym$h = mrb_intern_lit(mrb, "h");
  val$sym$h = mrb_symbol_value(sym$h);
  sym$anchor_x = mrb_intern_lit(mrb, "anchor_x");
  val$sym$anchor_x = mrb_symbol_value(sym$anchor_x);
  sym$anchor_y = mrb_intern_lit(mrb, "anchor_y");
  val$sym$anchor_y = mrb_symbol_value(sym$anchor_y);

	struct RClass *hash = mrb->hash_class;
	
  mrb_define_method_id(mrb, hash,
                       mrb_intern_lit(mrb, "normalize_posdata!"),
                       qtr_r_normalize_hash_b, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, hash,
                       mrb_intern_lit(mrb, "normalize_posdata"),
                       qtr_r_normalize_hash, MRB_ARGS_NONE());

	mrb_define_method_id(mrb, hash, mrb_intern_lit(mrb, "scale!"), qtr_r_scale_hash_b, MRB_ARGS_REQ(1));
	mrb_define_method_id(mrb, hash, mrb_intern_lit(mrb, "scale"), qtr_r_scale_hash, MRB_ARGS_REQ(1));
}

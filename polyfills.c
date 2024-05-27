#include "mruby.h"
#include "mruby/range.h"
#include <limits.h>

static mrb_value integer_aref(mrb_state *mrb, mrb_value self) {
  mrb_value int_range;
  mrb_int span = 0;
  mrb_bool second_given;

  mrb_get_args(mrb, "o|i?", &int_range, &span, &second_given);

  size_t value = mrb_integer(self);
  mrb_int begin;

  if (second_given) {
    begin = mrb_integer(mrb_to_int(mrb, int_range));
  } else if (mrb_integer_p(int_range)) {
    begin = 0;
    span = mrb_integer(int_range);
  } else if (mrb_range_p(int_range)) {
    struct RRange *range = mrb_range_ptr(mrb, int_range);
    begin = mrb_integer(mrb_to_int(mrb, range->beg));
    mrb_value endv = range->end;
    if (mrb_nil_p(endv)) {
      span = sizeof(size_t) * CHAR_BIT - begin + 1;
    } else {
      span = mrb_integer(mrb_to_int(mrb, endv)) - begin + (range->excl ? 0 : 1);
    }
  } else {
    mrb_raisef(mrb, E_TYPE_ERROR, "%Y cannot be converted to integer nor range",
               int_range);
  }

  if (span == 0)
    return mrb_int_value(mrb, 0);
  if (span < 0)
    return self;
  if (span > 63)
    return self;

  size_t mask = ((1 << span) - 1) << begin;
  return mrb_int_value(mrb, (value & mask) >> begin);
}

void drb_register_c_extensions_with_api(mrb_state *mrb, void *) {
  mrb_define_method_id(mrb, mrb->integer_class, mrb_intern_lit(mrb, "[]"),
                       integer_aref, MRB_ARGS_ARG(1, 1));
}

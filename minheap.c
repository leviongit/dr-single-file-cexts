#include <dragonruby.h>
#include <mruby.h>
#include <mruby/object.h>
#include <mruby/value.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  size_t size;
  size_t capa;
  mrb_value *data;
} minheap_t;

static mrb_sym gt_sym;
static mrb_sym lt_sym;

#define MAX_SENSIBLE_SHIFT_OF_1 63

minheap_t *minheap_new(mrb_state *mrb, uint8_t layers) {
  assert(layers <= MAX_SENSIBLE_SHIFT_OF_1);
  size_t size = (((size_t)(1)) << layers) - 1;
  minheap_t *minheap = mrb_malloc(mrb, sizeof(minheap_t));

  if (minheap == nullptr) {
    mrb_raisef(mrb, mrb->eStandardError_class, "failed to alloc minheap");
    __builtin_unreachable();
  }

  mrb_value *data = mrb_calloc(mrb, size, sizeof(mrb_value));

  if (data == nullptr) {
    mrb_raisef(mrb, mrb->eStandardError_class,
               "oom: not enough memory to allocate a %d-layer heap", layers);
    __builtin_unreachable();
  }

  *minheap = (minheap_t){
      .size = 0,
      .capa = size,
      .data = data,
  };
  return minheap;
}

void minheap_free(mrb_state *mrb, minheap_t *minheap) {
  if (minheap == nullptr)
    return;
  mrb_free(mrb, minheap->data);
  mrb_free(mrb, minheap);
}

[[clang::always_inline]] mrb_value minheap_get_top(const minheap_t *minheap) {
  if (minheap->size == 0)
    return mrb_nil_value();
  return minheap->data[0];
}

[[clang::always_inline]] size_t minheap_parent_idx(size_t idx) {
  return (idx - 1) / 2;
}

[[clang::always_inline]] size_t minheap_left_child_idx(size_t idx) {
  return 2 * idx + 1;
}

[[clang::always_inline]] size_t minheap_right_child_idx(size_t idx) {
  return 2 * idx + 2;
}

[[clang::always_inline]] mrb_bool
minheap_has_left_child(const minheap_t *minheap, size_t idx) {
  return minheap_left_child_idx(idx) <= minheap->size;
}

[[clang::always_inline]] mrb_bool
minheap_has_right_child(const minheap_t *minheap, size_t idx) {
  return minheap_right_child_idx(idx) <= minheap->size;
}

mrb_value minheap_get_left_child_of(const minheap_t *minheap, size_t idx) {
  size_t child_idx = minheap_left_child_idx(idx);
  if (idx > minheap->size)
    return mrb_nil_value();
  return minheap->data[child_idx];
}

mrb_value minheap_get_right_child_of(const minheap_t *minheap, size_t idx) {
  size_t child_idx = minheap_right_child_idx(idx);
  if (idx > minheap->size)
    return mrb_nil_value();
  return minheap->data[child_idx];
}

[[clang::always_inline]] mrb_bool
minheap_mrb_value_gtcmp(mrb_state *mrb, mrb_value left, mrb_value right) {
  return mrb_bool(mrb_funcall_argv(mrb, left, gt_sym, 1, &right));
}

[[clang::always_inline]] mrb_bool
minheap_mrb_value_ltcmp(mrb_state *mrb, mrb_value left, mrb_value right) {
  return mrb_bool(mrb_funcall_argv(mrb, left, lt_sym, 1, &right));
}

minheap_t *minheap_insert(mrb_state *mrb, minheap_t *minheap, mrb_value val) {
  if (minheap->size == minheap->capa) {
    mrb_value *new_data = mrb_realloc(
        mrb, minheap->data, (minheap->capa * 2 + 1) * sizeof(mrb_value));
    if (new_data == nullptr) {
      mrb_raisef(mrb, mrb->eStandardError_class,
                 "oom: not enough memory to reallocate a %d-layer heap",
                 __builtin_ctz(minheap->capa + 1));
      __builtin_unreachable();
    }
    minheap->capa *= 2;
    minheap->data = new_data;
  }

  size_t curr = minheap->size++;
  minheap->data[curr] = val;

  mrb_value *data = minheap->data;

  while (curr > 0 && minheap_mrb_value_gtcmp(
                         mrb, data[minheap_parent_idx(curr)], data[curr])) {
    const mrb_value tmp = data[minheap_parent_idx(curr)];
    data[minheap_parent_idx(curr)] = data[curr];
    data[curr] = tmp;

    curr = minheap_parent_idx(curr);
  }

  return minheap;
}

void minheap_heapify(mrb_state *mrb, minheap_t *minheap, size_t idx) {
  if (minheap->size <= 1)
    return;

  size_t small = idx;

  size_t left = minheap_left_child_idx(idx);
  size_t right = minheap_right_child_idx(idx);

  mrb_value *data = minheap->data;

  if (left < minheap->size &&
      minheap_mrb_value_ltcmp(mrb, data[left], data[idx])) {
    small = left;
  }

  if (right < minheap->size &&
      minheap_mrb_value_ltcmp(mrb, data[right], data[small])) {
    small = right;
  }

  if (small != idx) {
    const mrb_value tmp = data[idx];
    data[idx] = data[small];
    data[small] = tmp;
    minheap_heapify(mrb, minheap, small);
  }
}

minheap_t *minheap_delete_min(mrb_state *mrb, minheap_t *minheap) {
  if (minheap == nullptr || minheap->size == 0)
    return minheap;

  size_t size = minheap->size;
  mrb_value last = minheap->data[size - 1];

  minheap->data[0] = last;

  --minheap->size;

  minheap_heapify(mrb, minheap, 0);
  return minheap;
}

static const mrb_data_type minheap_datatype = {
    .struct_name = "Minheap#levi",
    .dfree = (void (*)(mrb_state *, void *))minheap_free};

mrb_value minheap_alloc_m(mrb_state *mrb, mrb_value klass) {
  minheap_t *minheap = minheap_new(mrb, 4);
  return mrb_obj_value(mrb_data_object_alloc(mrb, mrb_class_ptr(klass), minheap,
                                             &minheap_datatype));
}

mrb_value minheap_init_m(mrb_state *mrb, mrb_value self) {
  const mrb_value *args = nullptr;
  mrb_int ct = 0;
  mrb_get_args(mrb, "*!", &args, &ct);

  minheap_t *minheap = DATA_PTR(self);

  for (mrb_int i = 0; i < ct; ++i) {
    mrb_value val = args[i];
    mrb_gc_register(mrb, val);
    minheap_insert(mrb, minheap, val);
  }
  return mrb_nil_value();
}

mrb_value minheap_insert_m(mrb_state *mrb, mrb_value self) {
  const mrb_value arg = mrb_get_arg1(mrb);
  mrb_gc_register(mrb, arg);
  minheap_insert(mrb, mrb_data_check_get_ptr(mrb, self, &minheap_datatype),
                 arg);
  return self;
}

mrb_value minheap_peek_m(mrb_state *mrb, mrb_value self) {
  return minheap_get_top(mrb_data_check_get_ptr(mrb, self, &minheap_datatype));
}

mrb_value minheap_pop_m(mrb_state *mrb, mrb_value self) {
  minheap_t *minheap = mrb_data_check_get_ptr(mrb, self, &minheap_datatype);
  const mrb_value top = minheap_get_top(minheap);
  minheap_delete_min(mrb, minheap);
  mrb_gc_unregister(mrb, top);

  return top;
}

mrb_value minheap_to_a(mrb_state *mrb, const minheap_t *minheap) {
	return mrb_ary_new_from_values(mrb, minheap->size, minheap->data);
}

mrb_value minheap_to_a_m(mrb_state *mrb, mrb_value self) {
	const minheap_t *minheap = mrb_data_check_get_ptr(mrb, self, &minheap_datatype);
	return minheap_to_a(mrb, minheap);
}

mrb_value minheap_size_m(mrb_state *mrb, mrb_value self) {
	const minheap_t *minheap = mrb_data_check_get_ptr(mrb, self, &minheap_datatype);
	return mrb_int_value(mrb, minheap->size);
}

mrb_value minheap_empty_p_m(mrb_state *mrb, mrb_value self) {
	const minheap_t *minheap = mrb_data_check_get_ptr(mrb, self, &minheap_datatype);
	return mrb_bool_value(minheap->size == 0);
}

void drb_register_c_extensions_with_api(mrb_state *mrb, struct drb_api_t *) {
  gt_sym = mrb_intern_static(mrb, ">", 1);
  lt_sym = mrb_intern_static(mrb, "<", 1);

  struct RClass *minheap_cls =
      mrb_define_class(mrb, "MinHeap", mrb->object_class);

  MRB_SET_INSTANCE_TT(minheap_cls, MRB_TT_DATA);

  mrb_define_class_method(mrb, minheap_cls, "allocate", minheap_alloc_m,
                          MRB_ARGS_NONE());
  mrb_define_method(mrb, minheap_cls, "initialize", minheap_init_m,
                    MRB_ARGS_ANY());
  mrb_define_method(mrb, minheap_cls, "insert", minheap_insert_m,
                    MRB_ARGS_REQ(1));
  mrb_define_method(mrb, minheap_cls, "peek", minheap_peek_m, MRB_ARGS_NONE());
  mrb_define_method(mrb, minheap_cls, "pop", minheap_pop_m, MRB_ARGS_NONE());
  mrb_define_method(mrb, minheap_cls, "to_a", minheap_to_a_m, MRB_ARGS_NONE());
  mrb_define_method(mrb, minheap_cls, "size", minheap_size_m, MRB_ARGS_NONE());
  mrb_define_method(mrb, minheap_cls, "length", minheap_size_m, MRB_ARGS_NONE());
  mrb_define_method(mrb, minheap_cls, "empty?", minheap_empty_p_m, MRB_ARGS_NONE());
}

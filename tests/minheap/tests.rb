def test_minheap_initialize(_args, assert)
  heap = MinHeap.new(8, 1, 3)

  assert.equal! heap.peek, 1
  assert.equal! heap.pop, 1
  assert.equal! heap.peek, 3
  assert.equal! heap.pop, 3
  assert.equal! heap.peek, 8
  assert.equal! heap.pop, 8
  assert.equal! heap.peek, nil
  assert.equal! heap.pop, nil
end


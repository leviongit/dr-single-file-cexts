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

def test_minheap_custom_objects(_args, assert)
  heap = MinHeap.new

  heap.insert [10, 'Bob']
  heap.insert [5, :red]
  heap.insert [7, { hp: 100 }]

  assert.equal! heap.pop, [5, :red]
  assert.equal! heap.pop, [7, { hp: 100 }]
  assert.equal! heap.pop, [10, 'Bob']
end

def test_performance_test(_args, assert)
  numbers = (1..100_000).to_a.shuffle
  heap = MinHeap.new
  start_time = Time.now
  numbers.each { |n| heap.insert n }
  100_000.times { heap.pop }
  end_time = Time.now
  puts "Time elapsed #{(end_time - start_time) * 1000} milliseconds"
  assert.ok!
end

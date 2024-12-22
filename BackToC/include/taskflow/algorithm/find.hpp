#pragma once

#include "../taskflow.hpp"

namespace tf {

namespace detail {

// Function: find_if_loop
template <typename Iterator, typename Predicate>
bool find_if_loop(
  std::atomic<size_t>& offset, 
  Iterator& beg,
  size_t& prev_e,
  size_t  curr_b, 
  size_t  curr_e,
  Predicate predicate
) {
  // early prune
  if(offset.load(std::memory_order_relaxed) < curr_b) {
    return true;
  }
  std::advance(beg, curr_b - prev_e);
  for(size_t x = curr_b; x<curr_e; x++) {
    if(predicate(*beg++)) {
      atomic_min(offset, x);
      return true;
    }
  }
  prev_e = curr_e;
  return false;
}

// Function: find_if_not_loop
template <typename Iterator, typename Predicate>
bool find_if_not_loop(
  std::atomic<size_t>& offset, 
  Iterator& beg,
  size_t& prev_e,
  size_t  curr_b, 
  size_t  curr_e,
  Predicate predicate
) {

  // early prune
  if(offset.load(std::memory_order_relaxed) < curr_b) {
    return true;
  }
  std::advance(beg, curr_b - prev_e);
  for(size_t x = curr_b; x<curr_e; x++) {
    if(!predicate(*beg++)) {
      atomic_min(offset, x);
      return true;
    }
  }
  prev_e = curr_e;
  return false;
}

}  // namespace detail --------------------------------------------------------

// Function: make_find_if_task
template <typename B, typename E, typename T, typename UOP, typename P = DefaultPartitioner>
auto make_find_if_task(B first, E last, T& result, UOP predicate, P part = P()) {
  
  using namespace std::string_literals;
  
  using B_t = std::decay_t<unwrap_ref_decay_t<B>>;
  using E_t = std::decay_t<unwrap_ref_decay_t<E>>;

  return [=, &result] (Runtime& rt) mutable {

    // fetch the stateful values
    B_t beg = first;
    E_t end = last;

    size_t W = rt.executor().num_workers();
    size_t N = std::distance(beg, end);

    // only myself - no need to spawn another graph
    if(W <= 1 || N <= part.chunk_size()) {
      part([&](){ result = std::find_if(beg, end, predicate); })();
      return;
    }
    
    PreemptionGuard preemption_guard(rt);

    // use no more workers than the iteration count
    if(N < W) {
      W = N;
    }
    
    // we leverage smart pointer to let the last task update the result
    std::shared_ptr<std::atomic<size_t>> offset(
      new std::atomic<size_t>(N),
      [=, &result](std::atomic<size_t>* p) {
        result = std::next(beg, p->load(std::memory_order_relaxed));
        delete p;
      }
    );
    
    // static partitioner
    if constexpr(part.type() == PartitionerType::STATIC) {
      size_t chunk_size;
      for(size_t w=0, curr_b=0; w<W && curr_b < N;) {
        chunk_size = part.adjusted_chunk_size(N, W, w);
        auto task = part([=] () mutable {
          part.loop_until(N, W, curr_b, chunk_size,
            [=, &offset, prev_e=size_t{0}](size_t part_b, size_t part_e) mutable {
              return detail::find_if_loop(
                *offset, beg, prev_e, part_b, part_e, predicate
              );
            }
          );
          // must release the ownership before async is destroyed
          // as the node deletion comes after the join counter reaches zero
          offset.reset();
        });
        (++w == W || (curr_b += chunk_size) >= N) ? task() : rt.silent_async(task);
      }
    }
    // dynamic partitioner
    else {
      auto next = std::make_shared<std::atomic<size_t>>(0);
      for(size_t w=0; w<W;) {
        auto task = part([=] () mutable {
          part.loop_until(N, W, *next, 
            [=, &offset, prev_e=size_t{0}](size_t curr_b, size_t curr_e) mutable {
              return detail::find_if_loop(
                *offset, beg, prev_e, curr_b, curr_e, predicate
              );
            }
          );
          // must release the ownership before async is destroyed
          // as the node deletion comes after the join counter reaches zero
          offset.reset();
        });
        (++w == W) ? task() : rt.silent_async(task);
      }
    }
  };
}

// Function: make_find_if_not_task
template <typename B, typename E, typename T, typename UOP, typename P = DefaultPartitioner>
auto make_find_if_not_task(B first, E last, T& result, UOP predicate, P part = P()) {
  
  using namespace std::string_literals;
  
  using B_t = std::decay_t<unwrap_ref_decay_t<B>>;
  using E_t = std::decay_t<unwrap_ref_decay_t<E>>;

  return [=, &result] (Runtime& rt) mutable {

    // fetch the stateful values
    B_t beg = first;
    E_t end = last;

    size_t W = rt.executor().num_workers();
    size_t N = std::distance(beg, end);

    // only myself - no need to spawn another graph
    if(W <= 1 || N <= part.chunk_size()) {
      part([&](){ result = std::find_if_not(beg, end, predicate); })();
      return;
    }

    PreemptionGuard preemption_guard(rt);

    if(N < W) {
      W = N;
    }
    
    // we leverage smart pointer to let the last task update the result
    std::shared_ptr<std::atomic<size_t>> offset(
      new std::atomic<size_t>(N),
      [=, &result](std::atomic<size_t>* p) {
        result = std::next(beg, p->load(std::memory_order_relaxed));
        delete p;
      }
    );

    // static partitioner
    if constexpr(part.type() == PartitionerType::STATIC) {
      size_t chunk_size;
      for(size_t w=0, curr_b=0; w<W && curr_b < N;) {
        chunk_size = part.adjusted_chunk_size(N, W, w);
        auto task = part([=] () mutable {
          part.loop_until(N, W, curr_b, chunk_size,
            [=, &offset, prev_e=size_t{0}](size_t part_b, size_t part_e) mutable {
              return detail::find_if_not_loop(
                *offset, beg, prev_e, part_b, part_e, predicate
              );
            }
          );
          // must release the ownership before async is destroyed
          // as the node deletion comes after the join counter reaches zero
          offset.reset();
        });
        (++w == W || (curr_b += chunk_size) >= N) ? task() : rt.silent_async(task);
      }
    }
    // dynamic partitioner
    else {
      auto next = std::make_shared<std::atomic<size_t>>(0);
      for(size_t w=0; w<W;) {
        auto task = part([=] () mutable {
          part.loop_until(N, W, *next, 
            [=, &offset, prev_e=size_t{0}](size_t curr_b, size_t curr_e) mutable {
              return detail::find_if_not_loop(
                *offset, beg, prev_e, curr_b, curr_e, predicate
              );
            }
          );
          // must release the ownership before async is destroyed
          // as the node deletion comes after the join counter reaches zero
          offset.reset();
        });
        (++w == W) ? task() : rt.silent_async(task);
      }
    }
  };
}

// Function: make_min_element_task
template <typename B, typename E, typename T, typename C, typename P = DefaultPartitioner>
auto make_min_element_task(B first, E last, T& result, C comp, P part = P()) {
  
  using namespace std::string_literals;

  using B_t = std::decay_t<unwrap_ref_decay_t<B>>;
  using E_t = std::decay_t<unwrap_ref_decay_t<E>>;

  return [=, &result] (Runtime& rt) mutable {

    // fetch the iterator values
    B_t beg = first;
    E_t end = last;

    size_t W = rt.executor().num_workers();
    size_t N = std::distance(beg, end);

    // only myself - no need to spawn another graph
    if(W <= 1 || N <= part.chunk_size()) {
      part([&](){ result = std::min_element(beg, end, comp); })();
      return;
    }

    PreemptionGuard preemption_guard(rt);

    if(N < W) {
      W = N;
    }
    
    auto mutex = std::make_shared<std::mutex>();
    
    // initialize the result to the first element
    result = beg++;
    N--;

    // static partitioner
    if constexpr(part.type() == PartitionerType::STATIC) {
      
      size_t chunk_size;

      for(size_t w=0, curr_b=0; w<W && curr_b < N;) {
        
        // we force chunk size to be at least two because the temporary
        // variable sum needs to avoid copy at the first step
        chunk_size = std::max(size_t{2}, part.adjusted_chunk_size(N, W, w));
        
        auto task = part([=, &result] () mutable {
          std::advance(beg, curr_b);

          if(N - curr_b == 1) {
            std::lock_guard<std::mutex> lock(*mutex);
            if(comp(*beg, *result)) {
              result = beg;
            }
            return;
          }

          auto beg1 = beg++;
          auto beg2 = beg++;
          T smallest = comp(*beg1, *beg2) ? beg1 : beg2;
        
          // loop reduce
          part.loop(N, W, curr_b, chunk_size,
            [=, &smallest, prev_e=curr_b+2](size_t part_b, size_t part_e) mutable {

              if(part_b > prev_e) {
                std::advance(beg, part_b - prev_e);
              }
              else {
                part_b = prev_e;
              }

              for(size_t x=part_b; x<part_e; x++, beg++) {
                if(comp(*beg, *smallest)) {
                  smallest = beg;
                }
              }
              prev_e = part_e;
            }
          ); 
          
          // final reduce
          std::lock_guard<std::mutex> lock(*mutex);
          if(comp(*smallest, *result)) {
            result = smallest;
          }
        });
        
        (++w == W || (curr_b += chunk_size) >= N) ? task() : rt.silent_async(task);
      }
    }
    // dynamic partitioner
    else {
      auto next = std::make_shared<std::atomic<size_t>>(0);
      
      for(size_t w=0; w<W;) {

        auto task = part([=, &result] () mutable {
          // pre-reduce
          size_t s0 = next->fetch_add(2, std::memory_order_relaxed);

          if(s0 >= N) {
            return;
          }

          std::advance(beg, s0);

          if(N - s0 == 1) {
            std::lock_guard<std::mutex> lock(*mutex);
            if(comp(*beg, *result)) {
              result = beg;
            }
            return;
          }

          auto beg1 = beg++;
          auto beg2 = beg++;

          T smallest = comp(*beg1, *beg2) ? beg1 : beg2;
          
          // loop reduce
          part.loop(N, W, *next, 
            [=, &smallest, prev_e=s0+2](size_t part_b, size_t part_e) mutable {
              std::advance(beg, part_b - prev_e);
              for(size_t x=part_b; x<part_e; x++, beg++) {
                if(comp(*beg, *smallest)) {
                  smallest = beg;
                }
              }
              prev_e = part_e;
            }
          ); 
          
          // final reduce
          std::lock_guard<std::mutex> lock(*mutex);
          if(comp(*smallest, *result)) {
            result = smallest;
          }
        });
        (++w == W) ? task() : rt.silent_async(task);
      }
    }
  };
}

// Function: make_max_element_task
template <typename B, typename E, typename T, typename C, typename P = DefaultPartitioner>
auto make_max_element_task(B first, E last, T& result, C comp, P part = P()) {
  
  using namespace std::string_literals;

  using B_t = std::decay_t<unwrap_ref_decay_t<B>>;
  using E_t = std::decay_t<unwrap_ref_decay_t<E>>;

  return [=, &result] (Runtime& rt) mutable {

    // fetch the iterator values
    B_t beg = first;
    E_t end = last;

    size_t W = rt.executor().num_workers();
    size_t N = std::distance(beg, end);

    // only myself - no need to spawn another graph
    if(W <= 1 || N <= part.chunk_size()) {
      part([&](){ result = std::max_element(beg, end, comp); })();
      return;
    }

    PreemptionGuard preemption_guard(rt);

    if(N < W) {
      W = N;
    }

    auto mutex = std::make_shared<std::mutex>();
    
    // initialize the result to the first element
    result = beg++;
    N--;

    // static partitioner
    if constexpr(part.type() == PartitionerType::STATIC) {
      
      size_t chunk_size;

      for(size_t w=0, curr_b=0; w<W && curr_b < N;) {
        
        // we force chunk size to be at least two because the temporary
        // variable sum needs to avoid copy at the first step
        chunk_size = std::max(size_t{2}, part.adjusted_chunk_size(N, W, w));
        
        auto task = part([=, &result] () mutable {

          std::advance(beg, curr_b);

          if(N - curr_b == 1) {
            std::lock_guard<std::mutex> lock(*mutex);
            if(comp(*result, *beg)) {
              result = beg;
            }
            return;
          }

          auto beg1 = beg++;
          auto beg2 = beg++;
          T largest = comp(*beg1, *beg2) ? beg2 : beg1;
        
          // loop reduce
          part.loop(N, W, curr_b, chunk_size,
            [=, &largest, prev_e=curr_b+2](size_t part_b, size_t part_e) mutable {

              if(part_b > prev_e) {
                std::advance(beg, part_b - prev_e);
              }
              else {
                part_b = prev_e;
              }

              for(size_t x=part_b; x<part_e; x++, beg++) {
                if(comp(*largest, *beg)) {
                  largest = beg;
                }
              }
              prev_e = part_e;
            }
          ); 
          
          // final reduce
          std::lock_guard<std::mutex> lock(*mutex);
          if(comp(*result, *largest)) {
            result = largest;
          }
        });
        (++w == W || (curr_b += chunk_size) >= N) ? task() : rt.silent_async(task);
      }
    }
    // dynamic partitioner
    else {
      auto next = std::make_shared<std::atomic<size_t>>(0);
      
      for(size_t w=0; w<W;) {

        auto task = part([=, &result] () mutable {
          // pre-reduce
          size_t s0 = next->fetch_add(2, std::memory_order_relaxed);

          if(s0 >= N) {
            return;
          }

          std::advance(beg, s0);

          if(N - s0 == 1) {
            std::lock_guard<std::mutex> lock(*mutex);
            if(comp(*result, *beg)) {
              result = beg;
            }
            return;
          }

          auto beg1 = beg++;
          auto beg2 = beg++;

          T largest = comp(*beg1, *beg2) ? beg2 : beg1;
          
          // loop reduce
          part.loop(N, W, *next, 
            [=, &largest, prev_e=s0+2](size_t part_b, size_t part_e) mutable {
              std::advance(beg, part_b - prev_e);
              for(size_t x=part_b; x<part_e; x++, beg++) {
                if(comp(*largest, *beg)) {
                  largest = beg;
                }
              }
              prev_e = part_e;
            }
          ); 
          
          // final reduce
          std::lock_guard<std::mutex> lock(*mutex);
          if(comp(*result, *largest)) {
            result = largest;
          }
        });
        (++w == W) ? task() : rt.silent_async(task);
      }
    }
  };
}


// Function: find_if
template <typename B, typename E, typename T, typename UOP, typename P>
Task tf::FlowBuilder::find_if(B first, E last, T& result, UOP predicate, P part) {
  return emplace(make_find_if_task(first, last, result, predicate, part));
}

// Function: find_if_not
template <typename B, typename E, typename T, typename UOP, typename P>
Task tf::FlowBuilder::find_if_not(B first, E last, T& result, UOP predicate, P part) {
  return emplace(make_find_if_not_task(first, last, result, predicate, part));
}

// ----------------------------------------------------------------------------
// min_element
// ----------------------------------------------------------------------------

// Function: min_element
template <typename B, typename E, typename T, typename C, typename P>
Task FlowBuilder::min_element(B first, E last, T& result, C comp, P part) {
  return emplace(make_min_element_task(first, last, result, comp, part));
}

// ----------------------------------------------------------------------------
// max_element
// ----------------------------------------------------------------------------

// Function: max_element
template <typename B, typename E, typename T, typename C, typename P>
Task FlowBuilder::max_element(B first, E last, T& result, C comp, P part) {
  return emplace(make_max_element_task(first, last, result, comp, part));
}

}  // end of namespace tf -----------------------------------------------------
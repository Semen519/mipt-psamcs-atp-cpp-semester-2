#include <iostream>
#include "smart_pointers.h"
#include <cassert>
#include <vector>
#include <algorithm>

int main() {
  {
    std::cout << "--------------------------------------------\n";
    My::SharedPtr<int> a(new int(5));
    std::cout << "count_a = " << a.use_count() << '\n';
    auto b = a;
//  std::cout << std::is_same_v<decltype(b), decltype(a)> << '\n';
    std::cout << "count_a = " << a.use_count() << '\n';
    assert(a.use_count() == b.use_count());
    std::cout << "count_a = " << a.use_count() << ", count_b = "
              << b.use_count()
              << '\n';

    a = b;
    std::cout << "count_a = " << a.use_count() << ", count_b = "
              << b.use_count()
              << '\n';
  }
  {
    std::cout << "--------------------------------------------\n";
    auto p1 = My::make_shared<std::vector<int>>(std::vector<int>(10, -7));
    My::SharedPtr<std::vector<int>> p2(new std::vector<int>(13, 100));
    std::cout << "count_p1 = " << p1.use_count() << ", count_p2 = "
              << p2.use_count() << '\n';
    p2 = p1;
    std::cout << "count_p1 = " << p1.use_count() << ", count_p2 = "
              << p2.use_count() << '\n';
  }
  {
    using std::vector;

    auto first_ptr = My::SharedPtr<vector<int>>(new vector<int>(1'000'000));

    (*first_ptr)[0] = 1;

    vector<int>& vec = *first_ptr;
    auto second_ptr = My::SharedPtr<vector<int>>(new vector<int>(vec));

    (*second_ptr)[0] = 2;

    for (int i = 0; i < 1'000'000; ++i)
      first_ptr.swap(second_ptr);
    first_ptr->swap(*second_ptr);

    assert(first_ptr->front() == 2);
    assert(second_ptr->front() == 1);

    assert(first_ptr.use_count() == 1);
    assert(second_ptr.use_count() == 1);

    for (int i = 0; i < 10; ++i) {
      auto third_ptr = My::SharedPtr<vector<int>>(new vector<int>(vec));
      auto fourth_ptr = second_ptr;
      fourth_ptr.swap(third_ptr);
      assert(second_ptr.use_count() == 2);
    }

    assert(second_ptr.use_count() == 1);

    {
      vector<My::SharedPtr<vector<int>>> ptrs(10, My::SharedPtr<vector<int>>(first_ptr));
      for (int i = 0; i < 100'000; ++i) {
        ptrs.push_back(ptrs.back());
        ptrs.push_back(My::SharedPtr<vector<int>>(ptrs.back()));
      }
      assert(first_ptr.use_count() == 1 + 10 + 200'000);
    }

    first_ptr.reset(new vector<int>());
    second_ptr.reset();
    My::SharedPtr<vector<int>>().swap(first_ptr);

    assert(second_ptr.get() == nullptr);
    assert(second_ptr.get() == nullptr);

    for (int k = 0; k < 2; ++k) {
      vector<My::SharedPtr<int>> ptrs;
      for (int i = 0; i < 100'000; ++i) {
        int* p = new int(rand() % 99'999);
        ptrs.push_back(My::SharedPtr<int>(p));
      }
      std::sort(ptrs.begin(), ptrs.end(), [](auto&& x, auto&& y){return *x < *y;});
      for (int i = 0; i + 1 < 100'000; ++i) {
        assert(*(ptrs[i]) <= *(ptrs[i+1]));
      }
      while (!ptrs.empty()) {
        ptrs.pop_back();
      }
    }
  }
  std::cerr << "Test 1 (shared ptr) passed." << std::endl;
  return 0;
}

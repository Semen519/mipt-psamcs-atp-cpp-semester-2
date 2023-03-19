#ifndef SHAREDPTR__SMART_POINTERS_H_
#define SHAREDPTR__SMART_POINTERS_H_

#include <memory>
#include <cassert>

namespace My {

template<typename U, typename V>
using base_or_derived_t = std::enable_if_t<
    std::is_base_of_v<U, V> || std::is_same_v<U, V>>;

struct BaseControlBlock {
  size_t shared_count;
  size_t weak_count;

  BaseControlBlock(size_t sc, size_t wc) :
      shared_count(sc),
      weak_count(wc) {}

  void shared_release() {
    --shared_count;
    if (shared_count == 0) {
      dispose();
      if (weak_count == 0) {
        destroy();
      }
    }
  }

  void weak_release() {
    --weak_count;
    if (weak_count == 0 && shared_count == 0) {
      destroy();
    }
  }
  virtual void dispose() noexcept = 0;
  virtual void destroy() noexcept {
    delete this;
  }
  virtual ~BaseControlBlock() = default;
};

template<typename T, typename Allocator, typename Del>
struct ControlBlockDirect : BaseControlBlock {
  T *ptr;
  Allocator alloc;
  Del del;

  ControlBlockDirect(size_t sc, size_t wc,
                     T *ptr, const Allocator &alloc, const Del &del) :
      BaseControlBlock(sc, wc),
      ptr(ptr),
      alloc(alloc),
      del(del) {}

  void dispose() noexcept override {
    del.operator()(ptr);
  }

  void destroy() noexcept override {
    auto tmp_alloc =
        typename std::allocator_traits<Allocator>::template rebind_alloc<
            ControlBlockDirect>(alloc);
    tmp_alloc.deallocate(this, 1);
  }
};

template<typename T, typename Allocator>
struct ControlBlockMakeShared : BaseControlBlock {
  Allocator alloc;
  T obj;

  ControlBlockMakeShared(size_t sc, size_t wc,
                         const Allocator &alloc, T &&obj) :
      BaseControlBlock(sc, wc),
      alloc(alloc),
      obj(std::move(obj)) {}

  T *get_ptr() { return &obj; }

  void dispose() noexcept override {
    using AllocTraits = std::allocator_traits<Allocator>;
    AllocTraits::destroy(alloc, &obj);
  }

  void destroy() noexcept override {
    auto tmp_alloc =
        typename std::allocator_traits<Allocator>::template rebind_alloc<
            ControlBlockMakeShared>(alloc);
    tmp_alloc.deallocate(this, 1);
  }
};

template<typename U>
class WeakPtr;

template<typename T>
class SharedPtr {
 private:
  template<typename Allocator, typename Deleter>
  auto allocate_direct(T *arg_ptr, const Allocator &alloc, const Deleter &del) {
    auto cb_alloc =
        typename std::allocator_traits<Allocator>::template rebind_alloc<
            ControlBlockDirect<T, Allocator, Deleter>>(alloc);
    auto ptr = cb_alloc.allocate(1);
    cb_alloc.construct(ptr, 1, 0, arg_ptr, alloc, del);
    return ptr;
  }

 public:
  SharedPtr() :
      cb_(nullptr), ptr_(nullptr) {}

  template<typename Y, typename Deleter, typename Allocator,
      typename = base_or_derived_t<T, Y>>
  SharedPtr(Y *ptr, const Deleter &del, const Allocator &alloc) :
      cb_(allocate_direct(ptr, alloc, del)),
      ptr_(ptr) {
//    std::cout << "1\n";
  }

  template<typename Y>
  SharedPtr(Y *ptr) :
      SharedPtr(ptr, std::default_delete<T>(), std::allocator<T>()) {}

  template<typename Y, typename Deleter, typename = base_or_derived_t<T, Y>>
  SharedPtr(Y *ptr, const Deleter &del):
      SharedPtr(ptr, del, std::allocator<T>()) {}

  SharedPtr(const SharedPtr &other) : cb_(other.cb_), ptr_(other.ptr_) {
    ++(cb_->shared_count);
//    std::cout << "2\n";
  }

  template<typename Y, typename = base_or_derived_t<T, Y>>
  SharedPtr(const SharedPtr<Y> &other): cb_(other.cb_), ptr_(other.ptr_) {
    ++(cb_->shared_count);
//    std::cout << "3\n";
  }

  template<typename Y, typename = base_or_derived_t<T, Y>>
  SharedPtr(SharedPtr<Y> &&other): cb_(other.cb_), ptr_(other.ptr_) {
    other.cb_ = nullptr;
    other.ptr_ = nullptr;
//    std::cout << "4\n";
  }

//  SharedPtr &operator=(const SharedPtr &other) = delete;

  template<typename Y, typename = base_or_derived_t<T, Y>>
  SharedPtr &operator=(SharedPtr<Y> &other) {
    auto tmp_ptr = other;
    swap(tmp_ptr);
    return *this;
  }

  template<typename Y>
  SharedPtr &operator=(SharedPtr<Y> &&other) {
    auto tmp = std::move(other);
    swap(tmp);
    return *this;
  }

 private:
  template<typename Y, typename Allocator, typename... Args>
  friend auto allocate_shared(const Allocator &alloc, Args &&... args);

  template<typename Y, typename... Args>
  friend SharedPtr<Y> make_shared(Args &&... args);

  template<typename Allocator>
  SharedPtr(ControlBlockMakeShared<T, Allocator> *cb) :
      cb_(cb),
      ptr_(cb->get_ptr()) {}

  template<typename Y, base_or_derived_t<T, Y>>
  SharedPtr(WeakPtr<Y> &weak_ptr) :
      cb_(weak_ptr.cb_),
      ptr_(weak_ptr.ptr_) {
    ++(cb_->shared_count);
  }

 public:
  ~SharedPtr() {
//    std::cout << "shared_ptr destructor called\n";
    if (cb_) {
      cb_->shared_release();
    }
  }

 public:
  template<typename Y, typename = base_or_derived_t<T, Y>>
  void swap(SharedPtr<Y> &other) {
    std::swap(cb_, other.cb_);
    std::swap(ptr_, other.ptr_);
  }

  size_t use_count() const { return cb_->shared_count; }

  template<typename Y>
  void reset(Y *ptr) { SharedPtr<Y>(ptr).swap(*this); }

  void reset() noexcept { SharedPtr().swap(*this); }

  T &operator*() { return *ptr_; }

  T *operator->() { return ptr_; }

  T *get() { return ptr_; }

 private:
  BaseControlBlock *cb_{nullptr};
  T *ptr_{nullptr};

  template<typename U>
  friend
  class SharedPtr;

  template<typename U>
  friend
  class WeakPtr;
};

template<typename T, typename Allocator, typename... Args>
auto allocate_shared(const Allocator &alloc, Args &&... args) {
  auto cb_alloc =
      typename std::allocator_traits<Allocator>::template rebind_alloc<
          ControlBlockMakeShared<T, Allocator>>(
          alloc);
  auto ptr = cb_alloc.allocate(1);
  cb_alloc.construct(ptr, 1, 0, alloc, std::forward<Args>(args)...);
  return ptr;
}

template<typename T, typename... Args>
SharedPtr<T> make_shared(Args &&... args) {
  return My::allocate_shared<T>(std::allocator<T>(),
                                std::forward<Args>(args)...);
}

template<typename T>
class WeakPtr {
 public:
  template<typename U, typename = base_or_derived_t<T, U>>
  WeakPtr(SharedPtr<U> &shared_ptr):
      cb_(shared_ptr.cb_),
      ptr_(shared_ptr.ptr_) {
    ++(cb_->weak_count);
  }

  template<typename U, typename = base_or_derived_t<T, U>>
  WeakPtr(WeakPtr<U> &other):
      cb_(other.cb_),
      ptr_(other.ptr_) {
    ++(cb_->weak_count);
  }

  template<typename U, typename = base_or_derived_t<T, U>>
  WeakPtr(WeakPtr<U> &&other):
      cb_(other.cb_),
      ptr_(other.ptr_) {
    other.cb_ = nullptr;
    other.ptr_ = nullptr;
  }

  template<typename U, typename = base_or_derived_t<T, U>>
  WeakPtr &operator=(WeakPtr<U> &other) {
    auto tmp_ptr = other;
    swap(tmp_ptr);
    return *this;
  }

  template<typename U, typename = base_or_derived_t<T, U>>
  WeakPtr &operator=(WeakPtr<U> &&other) {
    auto tmp_ptr = std::move(other);
    swap(tmp_ptr);
    return *this;
  }

  ~WeakPtr() {
    if (cb_) {
      cb_->weak_release();
    }
  }

 public:
  template<typename U, base_or_derived_t<T, U>>
  void swap(WeakPtr<U> &other) {
    std::swap(cb_, other.cb_);
    std::swap(ptr_, other.ptr_);
  }

  bool expired() const noexcept { return cb_ && cb_->shared_count == 0; }

  SharedPtr<T> lock() const noexcept {
    return expired() ? SharedPtr<T>() : SharedPtr<T>(*this);
  }

 private:
  BaseControlBlock *cb_{nullptr};
  T *ptr_{nullptr};

  template<typename U>
  friend
  class WeakPtr;

  template<typename U>
  friend
  class SharedPtr;
};

} // My namespace
#endif //SHAREDPTR__SMART_POINTERS_H_

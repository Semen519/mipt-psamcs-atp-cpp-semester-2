#ifndef SHAREDPTR__SMART_POINTERS_H_
#define SHAREDPTR__SMART_POINTERS_H_

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

  ControlBlockMakeShared(size_t sc, size_t wc,
                         const Allocator &alloc) :
      BaseControlBlock(sc, wc),
      alloc(alloc) {}

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
//    cb_alloc.construct(ptr, 1, 0, arg_ptr, alloc, del);
    new(ptr) ControlBlockDirect<T, Allocator, Deleter>(1,
                                                       0,
                                                       arg_ptr,
                                                       alloc,
                                                       del);
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
  }

  template<typename Y>
  SharedPtr(Y *ptr) :
      SharedPtr(ptr, std::default_delete<T>(), std::allocator<T>()) {}

  template<typename Y, typename Deleter, typename = base_or_derived_t<T, Y>>
  SharedPtr(Y *ptr, const Deleter &del):
      SharedPtr(ptr, del, std::allocator<T>()) {}

  SharedPtr(const SharedPtr &other) : cb_(other.cb_), ptr_(other.ptr_) {
    if (cb_) {
      ++(cb_->shared_count);
    }
  }

  template<typename Y, typename = base_or_derived_t<T, Y>>
  SharedPtr(const SharedPtr<Y> &other): cb_(other.cb_), ptr_(other.ptr_) {
    ++(cb_->shared_count);
  }

  template<typename Y, typename = base_or_derived_t<T, Y>>
  SharedPtr(SharedPtr<Y> &&other): cb_(other.cb_), ptr_(other.ptr_) {
    other.cb_ = nullptr;
    other.ptr_ = nullptr;
  }

  template<typename Y, typename = base_or_derived_t<T, Y>>
  SharedPtr &operator=(SharedPtr<Y> &other) {
    auto tmp_ptr = SharedPtr<T>(other);
    swap(tmp_ptr);
    return *this;
  }

  template<typename Y>
  SharedPtr &operator=(SharedPtr<Y> &&other) {
    SharedPtr<T>(std::move(other)).swap(*this);
    return *this;
  }

 private:
  template<typename Y, typename Allocator, typename... Args>
  friend auto allocateShared(const Allocator &alloc, Args &&... args);

  template<typename Y, typename... Args>
  friend SharedPtr<Y> makeShared(Args &&... args);

  template<typename Allocator>
  SharedPtr(ControlBlockMakeShared<T, Allocator> *cb) :
      cb_(cb),
      ptr_(cb->get_ptr()) {}

  SharedPtr(const WeakPtr<T> &weak_ptr) :
      cb_(weak_ptr.cb_),
      ptr_(weak_ptr.ptr_) {
    ++(cb_->shared_count);
  }

 public:
  ~SharedPtr() {
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

  T &operator*() noexcept { return *ptr_; }
  T &operator*() const noexcept { return *ptr_; }

  T *operator->() noexcept { return ptr_; }
  T *operator->() const noexcept { return ptr_; }

  T *get() noexcept { return ptr_; }
  T *get() const noexcept { return ptr_; }

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
auto allocateShared(const Allocator &alloc, Args &&... args) {
  auto cb_alloc =
      typename std::allocator_traits<Allocator>::template rebind_alloc<
          ControlBlockMakeShared<T, Allocator>>(
          alloc);
  auto ptr = cb_alloc.allocate(1);
  cb_alloc.construct(ptr, 1, 0, alloc, std::forward<Args>(args)...);
  return SharedPtr<T>(ptr);
}

template<typename T, typename... Args>
SharedPtr<T> makeShared(Args &&... args) {
  return allocateShared<T>(std::allocator<T>(),
                           std::forward<Args>(args)...);
}

template<typename T>
class WeakPtr {
 public:
  WeakPtr() :
      cb_(nullptr),
      ptr_(nullptr) {}

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
    auto tmp_ptr = WeakPtr<T>(other);
    swap(tmp_ptr);
    return *this;
  }

  template<typename U, typename = base_or_derived_t<T, U>>
  WeakPtr &operator=(WeakPtr<U> &&other) {
    WeakPtr<T>(std::move(other)).swap(*this);
    return *this;
  }

  template<typename U, typename = base_or_derived_t<T, U>>
  WeakPtr &operator=(SharedPtr<U> &shared_ptr) {
    WeakPtr<T>(shared_ptr).swap(*this);
    return *this;
  }

  ~WeakPtr() {
    if (cb_) {
      cb_->weak_release();
    }
  }

 public:
  template<typename U, typename = base_or_derived_t<T, U>>
  void swap(WeakPtr<U> &other) {
    std::swap(cb_, other.cb_);
    std::swap(ptr_, other.ptr_);
  }

  bool expired() const noexcept { return cb_ && cb_->shared_count == 0; }

  SharedPtr<T> lock() const noexcept {
    return expired() ? SharedPtr<T>() : SharedPtr<T>(*this);
  }

  size_t use_count() const noexcept { return cb_->shared_count; }

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

#endif //SHAREDPTR__SMART_POINTERS_H_

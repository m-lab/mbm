#ifndef SCOPED_PTR_H_
#define SCOPED_PTR_H_

template<typename T>
class scoped_ptr {
 public:
  explicit scoped_ptr(T* ptr) : ptr_(ptr) { }
  ~scoped_ptr() { delete ptr_; }

  T* operator->() { return ptr_; }
  const T* operator->() const { return ptr_; }

 private:
  T* ptr_;
};

#endif  // SCOPED_PTR_H_

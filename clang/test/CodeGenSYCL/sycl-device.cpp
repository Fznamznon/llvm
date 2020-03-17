// RUN: %clang_cc1 -fsycl -fsycl-is-device -triple spir64-unknown-unknown-sycldevice -disable-llvm-passes -emit-llvm %s -o - | FileCheck %s
// Test code generation for sycl_device attribute.

int bar(int b);

class A {
public:
  // CHECK-DAG: define linkonce_odr spir_func void @_ZN1A3fooEv
  __attribute__((sycl_device)) void foo() {}

  // CHECK-DAG: define linkonce_odr spir_func void @_ZN1AC1Ev
  __attribute__((sycl_device))
  A() {}
  // CHECK-DAG: define linkonce_odr spir_func void @_ZN1AD1Ev
  __attribute__((sycl_device)) ~A() {}

  template <typename T>
  __attribute__((sycl_device)) void AFoo(T t) {}

  // Templates are emitted when they are instantiated
  // CHECK-DAG: define linkonce_odr spir_func void @_ZN1A4AFooIiEEvT_
  template <>
  __attribute__((sycl_device)) void AFoo<int>(int t) {}
};

template <typename T>
struct B {
  T data;
  B(T _data) : data(_data) {}

  __attribute__((sycl_device)) void BFoo(T t) {}
};

template <>
struct B<int> {
  int data;
  B(int _data) : data(_data) {}

  // CHECK-DAG: _ZN1BIiE4BFooEi
  __attribute__((sycl_device)) void BFoo(int t) {}
};

// CHECK-DAG: define spir_func i32 @_Z3fooii
__attribute__((sycl_device))
int foo(int a, int b) { return a + bar(b); }

// CHECK-DAG: define spir_func i32 @_Z3bari
int bar(int b) { return b; }

// CHECK-DAG: define spir_func i32 @_Z3fari
int far(int b) { return b; }

// CHECK-DAG: define spir_func i32 @_Z3booii
__attribute__((sycl_device))
int boo(int a, int b) { return a + far(b); }

// CHECK-DAG: define spir_func i32 @_Z3cari
__attribute__((sycl_device))
int car(int b);
int car(int b) { return b; }

// CHECK-DAG: define spir_func i32 @_Z3cazi
int caz(int b);
__attribute__((sycl_device))
int caz(int b) { return b; }

template<typename T>
__attribute__((sycl_device))
void taf(T t) {}

// CHECK-DAG: define weak_odr spir_func void @_Z3tafIiEvT_
template void taf<int>(int t);

// CHECK-DAG: define spir_func void @_Z3tafIcEvT_
template<> void taf<char>(char t) {}

template<typename T>
void tar(T t) {}

// CHECK-DAG: define spir_func void @_Z3tarIcEvT_
template<>
__attribute__((sycl_device))
void tar<char>(char t) {}

// CHECK-NOT: @_Z3tarIiEvT_
template void tar<int>(int t);

// CHECK-NOT: @_Z3gooi
int goo(int b) { return b; }

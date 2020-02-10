// RUN: %clang_cc1 -triple spir64 -fsycl -fsycl-is-device -verify -fsyntax-only %s
// RUN: %clang_cc1 -fsycl -fsycl-is-device -fsyntax-only %s

template <class T>
class Z {
public:
  // TODO: If T is __float128 This might be a problem
  T field;
  //expected-error@+1 2{{'__float128' is not supported on this target}}
  __float128 field1;
};

void host_ok(void) {
  __float128 A;
  int B = sizeof(__float128);
  Z<__float128> C;
  C.field1 = A;
}

void usage() {
  //expected-error@+1 5{{'__float128' is not supported on this target}}
  __float128 A;
  Z<__float128> C;
  //expected-error@+2 {{'A' is unavailable}}
  //expected-error@+1 {{'field1' is unavailable}}
  C.field1 = A;
  //expected-error@+1 {{'A' is unavailable}}
  decltype(A) D;

  //expected-error@+1 {{'A' is unavailable}}
  auto foo1 = [=]() {
    //expected-error@+1 {{'__float128' is not supported on this target}}
    __float128 AA;
    //expected-error@+1 {{'A' is unavailable}}
    auto BB = A;
    BB += 1;
  };

  //expected-note@+1 {{called by 'usage'}}
  foo1();
}

template <typename t>
void foo2(){};

//expected-error@+1 {{'__float128 (__float128)' is not supported on this target}}
__float128 foo(__float128 P) { return P; }

template <typename Name, typename Func>
__attribute__((sycl_kernel)) void kernel(Func kernelFunc) {
  //expected-note@+1 5{{called by 'kernel}}
  kernelFunc();
  //expected-error@+1 {{'__float128' is not supported on this target}}
  __float128 A;
}

int main() {
  //expected-error@+1 2{{'__float128' is not supported on this target}}
  __float128 CapturedToDevice = 1;
  host_ok();
  kernel<class variables>([=]() {
    //expected-error@+1 {{'CapturedToDevice' is unavailable}}
    decltype(CapturedToDevice) D;
    //expected-error@+1 {{'CapturedToDevice' is unavailable}}
    auto C = CapturedToDevice;
    //expected-error@+1 {{'__float128' is not supported on this target}}
    __float128 BBBB;
    Z<__float128> S;
    //expected-error@+1 {{'field1' is unavailable}}
    S.field1 += 1;
    S.field = 1;
  });

  kernel<class functions>([=]() {
    //expected-note@+1 2{{called by 'operator()'}}
    usage();
    // expected-error@+1 2{{'__float128' is not supported on this target}}
    __float128 BBBB;
    // expected-error@+2 {{'BBBB' is unavailable}}
    // expected-error@+1 {{'foo' is unavailable}}
    auto A = foo(BBBB);
  });

  kernel<class ok>([=]() {
    Z<__float128> S;
    foo2<__float128>();
    // TODO: this shouldn't be diagnosed
    // expected-error@+1 {{'__float128' is not supported on this target}}
    int E = sizeof(__float128);
  });
  return 0;
}

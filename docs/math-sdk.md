# Fixed-Size Math SDK Plan

## Current host integration

Morpheus pins the MIT-licensed `third_party/la` repository and compiles its
stb-style header together with Morpheus-owned dynamic numerical algorithms as
`libmorpheus_math.a`. The archive is a private dependency of
`morpheus_runtime_core`; generated modules do not receive the vendor header,
generic symbol names, anonymous-union types, or host allocation ownership.

The backend is allocation-free and provides 2D, 3D, and 4D vectors plus 2x2,
3x3, and 4x4 square matrices for `float`, `double`, signed integer, and unsigned
integer values. Its strengths are graphics, geometry, and small fixed-size
calculations. It does not provide dynamic matrices, general linear-system
solvers, decompositions, eigenvalues, or regression.

## Proposed public SDK

Fixed-size arithmetic is pure, inexpensive, and needs no privileged host state.
It should therefore use a namespaced inline header, `include/morpheus/math.h`,
rather than a large capability table and a function-pointer call for every
vector addition.

The first version should expose Morpheus-owned types rather than aliases of the
vendor unions:

```c
typedef struct morph_vec2f { float x, y; } morph_vec2f;
typedef struct morph_vec3f { float x, y, z; } morph_vec3f;
typedef struct morph_vec4f { float x, y, z, w; } morph_vec4f;

typedef struct morph_mat3f { float values[9]; } morph_mat3f;
typedef struct morph_mat4f { float values[16]; } morph_mat4f;
```

Version 1 should remain deliberately small:

- vector construction, add, subtract, component multiply, and scalar multiply
- dot product, squared length, length, normalize-with-fallback, and reflection
- 3D cross product
- matrix identity, zero, multiplication, and matrix-vector transform
- 2D rotation and 3D/4D axis rotations
- explicit row-major storage and column-vector multiplication conventions

Float types should come first because they match rendering and geometry uses.
Double variants can be added when an application demonstrates a precision need.
Integer vector operations should wait; signed overflow and division/modulo by
zero complicate an otherwise total, side-effect-free interface.

## Boundary rules

- Public declarations and names use only the `morph_` prefix.
- Vendor types and function names remain private implementation details.
- No vendor struct is passed between TinyCC and Clang by value.
- Inline functions must not allocate, assert, exit, or retain pointers.
- Zero-length normalization takes an explicit fallback value.
- Matrix layout and multiplication order are documented and tested.
- Scalar square-root and trigonometric support must use a deliberately exposed
  Morpheus math surface; adding the entire system math library to TinyCC's
  symbol allowlist is not implicit.

These rules allow the SDK implementation to be generated from `la`, written
directly, or replaced with SIMD without changing application source.

## Validation before publication

1. Compare representative operations under Clang and TinyCC.
2. Test identity, composition order, 90-degree rotations, cross-product
   handedness, normalization fallback, and float tolerances.
3. Assert public type sizes and field offsets in both compilers.
4. Compile a generated-module fixture using only `morpheus/math.h`.
5. Verify frozen builds inline or dead-strip unused operations.

Dynamic matrices, QR/LU/SVD, regression, and large workloads belong in a
separate future `dev.morpheus.runtime.numeric` capability. That service can use
bounded buffers, explicit statuses, background jobs, and platform acceleration
without burdening the small fixed-size SDK.

## Current dynamic host interface

`src/host/math_host.h` combines the fixed-size declarations with an owned,
row-major `double` vector/matrix layer. The initial implementation provides:

- checked construction, copying, slicing, arithmetic, transpose, products, and
  matrix-vector products;
- packed Householder QR factorization, unpacking, square solves, and tall-matrix
  least-squares solves;
- linear regression with predictions for arbitrary new row counts and residual
  standard deviation based on `observations - predictors` degrees of freedom;
- Jacobi eigenvalue/eigenvector decomposition for bounded real symmetric
  matrices, with explicit non-convergence reporting;
- copying conversions between dynamic values and `la`'s `V2d`/`V3d`/`V4d` and
  `M2d`/`M3d`/`M4d` types.

All allocating outputs must be zero-initialized and explicitly disposed. Every
operation returns a stable status for invalid input, dimension mismatch,
oversized allocation, memory failure, rank deficiency, or non-convergence. The
temporary reference checkout under `third_party/tmp` is not a build dependency.

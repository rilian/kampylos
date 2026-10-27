---
id: PAT-026
title: The order of equal keys after sorting differs between C++ standard libraries and C runtimes
status: active
severity: low
platforms: [all]
area: determinism
applies_to: code whose output order or selection depends on std::sort, std::partial_sort, std::nth_element, std::make_heap or qsort with possible ties
origin: planodia PLA-011
reference_fix:
added: 2026-10-27
---

## The defect

`std::sort`, `std::partial_sort`, `std::nth_element`, heaps and `qsort` do not define the order of elements that
compare equal, and libstdc++ (GCC, MinGW GCC), libc++ (Apple clang, Android NDK, llvm-mingw) and the MSVC STL order
them differently. When two candidates tie (same score, same power), the platforms report them in a different order
or keep a different one of them in a "top N", so results differ across platforms although every number is
identical. Validators that compare lines, and resumes that mix builds, then disagree.

## Why it happens

Unstable sorts are allowed to break ties any way; each library uses a different algorithm. Ties are rare in real
data but common after rounding, in synthetic tests, and with integer scores.

## How to check a repository

```
git grep -nE 'std::(sort|partial_sort|nth_element|make_heap|sort_heap|priority_queue)|(^|[^A-Za-z0-9_])qsort *\('
```

For each call that decides output order or which elements are kept: can two elements compare equal, and does the
comparator then fall back to a unique key?

* `affected`: ties are possible and the comparator has no unique tie-breaker, and the order or selection reaches the
  output.
* `not-affected`: the comparator ends in a unique key (index, id), or `std::stable_sort` on input in a defined order,
  or the result does not reach the output.
* `n/a`: no sorting that reaches the output.

## How to fix

Add a unique last key to the comparator (the element's index or id). This can change which of two tied elements is
reported, so it changes results: record the change and its measured effect, and coordinate it with the validator.

## How to test

An input with deliberate ties, run with a libstdc++ build and a libc++ build: the outputs must be identical.

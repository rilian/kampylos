---
id: PAT-027
title: Unchecked GPU transfer and kernel-argument calls let a failure pass as stale or garbage results
status: active
severity: medium
platforms: [cuda, opencl, metal]
area: gpu-runtime
applies_to: host code that copies data to or from a GPU, sets kernel arguments, or launches kernels
check: 'clEnqueue(Read|Write|NDRange|Copy)|clSetKernelArg|clFinish|cudaMemcpy|cudaMemset|<<<|cudaGetLastError|cudaDeviceSynchronize|commandBuffer.*(status|error)'
origin: planodia PLA-015
reference_fix:
added: 2026-10-27
---

## The defect

`clEnqueueReadBuffer`, `clEnqueueWriteBuffer`, `clSetKernelArg`, `clEnqueueNDRangeKernel`, `cudaMemcpy`,
`cudaGetLastError` after a launch, or a Metal command buffer's status are not checked. When one fails (out of
resources, a driver reset, an invalid argument on one vendor only), the host reads back the previous contents of its
buffer and reports them as the result: wrong results that look plausible, accepted if the quorum partner has the
same fault or the validator is tolerant.

## Why it happens

Error checks are added to the setup code and forgotten in the hot loop; on the developer's GPU nothing ever fails.

## How to check a repository

```
git grep -nE 'clEnqueue(Read|Write|NDRange|Copy)|clSetKernelArg|clFinish|cudaMemcpy|cudaMemset|<<<|cudaGetLastError|cudaDeviceSynchronize|commandBuffer.*(status|error)'
```

* `affected`: any transfer, argument or launch whose result is ignored on the path that produces output.
* `not-affected`: every call is checked and a failure ends the item with an error (or a temporary exit, PAT-013),
  never with output.
* `n/a`: no GPU code.

## How to fix

Check every call; for CUDA check `cudaGetLastError()` after each launch and the result of the synchronising copy.
Clear host result buffers (or fill them with NaN) before reading back, so a failed read cannot pass old data.

## How to test

A test build that makes one read fail (wrong size, released buffer) must report an error, not a result.

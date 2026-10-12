// OpenCL equivalent of gpu_fit_binary.cu -- same batched Nelder-Mead driving logic (see that
// file's own top comment for the full design rationale: NelderMeadStepper instances advanced in
// lockstep rounds, every active stepper's pending points batched into one kernel launch per
// round, kampylos_gpu_complete.h patching in the few per-candidate points that need the real
// VBMicrolensing finite-source treatment), just dispatched through cl_api instead of the CUDA
// runtime. Embedded kernel source (kampylos_cl_embedded.h) is the SAME kampylos_kernel.cl
// validated directly against the CUDA version and the CPU reference
// (tests/test_kampylos_opencl.cpp) -- this file only adds the OpenCL plumbing around it.
#include <cstdio>
#include <cstring>
#define _USE_MATH_DEFINES
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include "kampylos_gpu_types.h"
#include "kampylos_gpu_complete.h"
#include "kampylos_cpu_patch_pool.h"
#include "kampylos_opencl_fit.h"
#include "nelder_mead_stepper.h"
#include "kampylos_batched_fit.h"
#include "kampylos_cl_embedded.h"

// 2026-10-06, forum thread 64 ("Intel GPU tasks run on Nvidia") -- see opencl_search.c's
// (Potamos's) identical fix for the full writeup. Each vendor-specific opencl_* plan_class now
// gets its own physical binary with this baked in at compile time.
#ifndef EXPECTED_GPU_VENDOR
#define EXPECTED_GPU_VENDOR ""
#endif

static bool platform_vendor_matches(cl_platform_id p) {
    return cl_platform_matches_vendor(p, EXPECTED_GPU_VENDOR) != 0;
}

[[noreturn]] static void opencl_fail(const char* what, cl_int err);

OpenCLFitContext::~OpenCLFitContext() {
    if (d_data) cl_api.ReleaseMemObject(d_data);
    if (d_cand) cl_api.ReleaseMemObject(d_cand);
    if (d_res) cl_api.ReleaseMemObject(d_res);
    if (kernel) cl_api.ReleaseKernel(kernel);
    if (program) cl_api.ReleaseProgram(program);
    if (queue) cl_api.ReleaseCommandQueue(queue);
    if (context) cl_api.ReleaseContext(context);
}

bool opencl_fit_select_device(OpenCLFitContext& ctx, int device_id, std::string* err_out) {
    if (!cl_dynload_init()) {
        if (err_out) *err_out = "could not load an OpenCL ICD on this host";
        return false;
    }

    cl_uint n_platforms = 0;
    cl_api.GetPlatformIDs(0, NULL, &n_platforms);
    if (n_platforms == 0) {
        if (err_out) *err_out = "no OpenCL platforms found";
        return false;
    }
    std::vector<cl_platform_id> platforms(n_platforms);
    cl_api.GetPlatformIDs(n_platforms, platforms.data(), NULL);

    // Same robust platform scan as opencl_search.c's gpu_search_init() (real forum bug history,
    // see that file's own comment): don't just trust platform index 0 -- scan for the first
    // platform that actually reports a device.
    cl_platform_id platform = 0;
    cl_uint n_devices = 0;
    for (cl_uint i = 0; i < n_platforms; i++) {
        if (!platform_vendor_matches(platforms[i])) continue;
        cl_uint n = 0;
        if (cl_api.GetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, 0, NULL, &n) == CL_SUCCESS && n > 0) {
            platform = platforms[i];
            n_devices = n;
            break;
        }
    }
    if (!platform) {
        if (err_out) *err_out = EXPECTED_GPU_VENDOR[0]
            ? (std::string("no \"") + EXPECTED_GPU_VENDOR + "\" OpenCL devices found on any platform")
            : "no OpenCL devices found on any platform";
        return false;
    }
    std::vector<cl_device_id> devices(n_devices);
    cl_api.GetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, n_devices, devices.data(), NULL);
    if (device_id < 0 || (cl_uint)device_id >= n_devices) device_id = 0;
    ctx.device = devices[device_id];

    char name[256] = {0};
    cl_api.GetDeviceInfo(ctx.device, CL_DEVICE_NAME, sizeof(name), name, NULL);
    fprintf(stderr, "opencl_fit_select_device: using OpenCL device \"%s\"\n", name);

    cl_int err;
    ctx.context = cl_api.CreateContext(NULL, 1, &ctx.device, NULL, NULL, &err);
    if (!ctx.context) { if (err_out) *err_out = "clCreateContext failed"; return false; }
    ctx.queue = cl_api.CreateCommandQueue(ctx.context, ctx.device, 0, &err);
    if (!ctx.queue) { if (err_out) *err_out = "clCreateCommandQueue failed"; return false; }

    const char* sources[2] = { kKampylosClTypesSource, kKampylosClKernelSource };
    size_t lengths[2] = { strlen(kKampylosClTypesSource), strlen(kKampylosClKernelSource) };
    ctx.program = cl_api.CreateProgramWithSource(ctx.context, 2, sources, lengths, &err);
    if (!ctx.program) { if (err_out) *err_out = "clCreateProgramWithSource failed"; return false; }
    err = cl_api.BuildProgram(ctx.program, 1, &ctx.device, "-cl-std=CL1.2", NULL, NULL);
    if (err != CL_SUCCESS) {
        size_t log_size = 0;
        cl_api.GetProgramBuildInfo(ctx.program, ctx.device, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
        std::vector<char> log(log_size + 1);
        cl_api.GetProgramBuildInfo(ctx.program, ctx.device, CL_PROGRAM_BUILD_LOG, log_size, log.data(), NULL);
        log[log_size] = 0;
        fprintf(stderr, "opencl_fit_select_device: kernel build failed:\n%s\n", log.data());
        if (err_out) *err_out = "kernel build failed";
        return false;
    }
    ctx.kernel = cl_api.CreateKernel(ctx.program, "kampylos_eval_candidates", &err);
    if (!ctx.kernel) { if (err_out) *err_out = "clCreateKernel failed"; return false; }

    // Work-group size (2026-10-12). It was a hard-coded 128, which every desktop GPU allows but
    // not every OpenCL device: CL_DEVICE_MAX_WORK_GROUP_SIZE is 64 on some embedded/older GPUs,
    // and a register-heavy kernel like this one (the quintic solver, all in double) can be
    // limited below the device maximum by the driver (CL_KERNEL_WORK_GROUP_SIZE). Since the
    // error checks of 2026-10-10 such a host failed every launch with CL_INVALID_WORK_GROUP_SIZE
    // (before, it silently used the previous round's results). The device maximum is applied
    // here; the kernel-specific limit is only known through clGetKernelWorkGroupInfo, which the
    // loader table (cl_dynload.h) does not carry, so that case is caught at the first launch
    // instead (opencl_eval_batch halves the size on CL_INVALID_WORK_GROUP_SIZE). The kernel
    // reads get_local_size(), so any power of two works; the per-candidate sums are then added
    // in a different order, a rounding-level difference that is the same for every round of a
    // run and its resume on the same device.
    ctx.local_size = 128;
    size_t max_wg = 0;
    if (cl_api.GetDeviceInfo(ctx.device, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(max_wg), &max_wg, NULL) == CL_SUCCESS && max_wg > 0)
        while ((size_t)ctx.local_size > max_wg && ctx.local_size > 1) ctx.local_size /= 2;
    if (ctx.local_size < 128)
        fprintf(stderr, "opencl_fit_select_device: work-group size %d (device maximum %lu)\n", ctx.local_size, (unsigned long)max_wg);
    return true;
}

void opencl_fit_upload_light_curve(OpenCLFitContext& ctx, const std::vector<DataPoint>& data) {
    // Flux-centred, same as gpu_upload_light_curve() (see kampylos_flux_offset()).
    ctx.flux_offset = kampylos_flux_offset(data);
    ctx.fs_bound = flux_fit_bound(data);
    ctx.h_data.resize(data.size());
    for (size_t i = 0; i < data.size(); i++) ctx.h_data[i] = { data[i].t, data[i].flux - ctx.flux_offset, data[i].sigma };
    ctx.n_points = (int)ctx.h_data.size();
    if (ctx.d_data) cl_api.ReleaseMemObject(ctx.d_data);
    cl_int err;
    ctx.d_data = cl_api.CreateBuffer(ctx.context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        ctx.h_data.size() * sizeof(LightCurvePoint), ctx.h_data.data(), &err);
    if (!ctx.d_data) opencl_fail("clCreateBuffer(light curve)", err);
}

// Every OpenCL call's return code is checked (2026-10-10): a failed write, launch or read used
// to go unnoticed, the stale d_res from the previous round was read back as if it were this
// round's result and the fit went on with it. A failure is reported with its code and raised as
// std::runtime_error; the wrappers turn that into a clean exit (BOINC restarts the task from its
// checkpoint) rather than a cell computed from garbage.
[[noreturn]] static void opencl_fail(const char* what, cl_int err) {
    char buf[200];
    snprintf(buf, sizeof(buf), "%s failed with OpenCL error %d", what, (int)err);
    fprintf(stderr, "opencl_fit: %s\n", buf);
    throw std::runtime_error(buf);
}
#define CL_CHECK(what, call) do { cl_int _e = (call); if (_e != CL_SUCCESS) opencl_fail(what, _e); } while (0)

static void ensure_cand_capacity(OpenCLFitContext& ctx, int n) {
    if (n <= ctx.cand_capacity) return;
    if (ctx.d_cand) cl_api.ReleaseMemObject(ctx.d_cand);
    if (ctx.d_res) cl_api.ReleaseMemObject(ctx.d_res);
    cl_int err;
    ctx.d_cand = cl_api.CreateBuffer(ctx.context, CL_MEM_READ_ONLY, n * sizeof(Candidate), NULL, &err);
    if (!ctx.d_cand) opencl_fail("clCreateBuffer(candidates)", err);
    ctx.d_res = cl_api.CreateBuffer(ctx.context, CL_MEM_WRITE_ONLY, n * sizeof(CandidateResult), NULL, &err);
    if (!ctx.d_res) opencl_fail("clCreateBuffer(results)", err);
    ctx.cand_capacity = n;
}

static std::vector<double> opencl_eval_batch(
    OpenCLFitContext& ctx, double s, double q,
    const std::vector<Candidate>& points
) {
    int n = (int)points.size();
    std::vector<double> chi2(n);
    if (n == 0) return chi2;

    ensure_cand_capacity(ctx, n);
    CL_CHECK("clEnqueueWriteBuffer(candidates)",
             cl_api.EnqueueWriteBuffer(ctx.queue, ctx.d_cand, CL_TRUE, 0, n * sizeof(Candidate), points.data(), 0, NULL, NULL));

    CL_CHECK("clSetKernelArg(0)", cl_api.SetKernelArg(ctx.kernel, 0, sizeof(cl_mem), &ctx.d_data));
    CL_CHECK("clSetKernelArg(1)", cl_api.SetKernelArg(ctx.kernel, 1, sizeof(int), &ctx.n_points));
    CL_CHECK("clSetKernelArg(2)", cl_api.SetKernelArg(ctx.kernel, 2, sizeof(double), &s));   // linear s, q (2026-10-08)
    CL_CHECK("clSetKernelArg(3)", cl_api.SetKernelArg(ctx.kernel, 3, sizeof(double), &q));
    CL_CHECK("clSetKernelArg(4)", cl_api.SetKernelArg(ctx.kernel, 4, sizeof(cl_mem), &ctx.d_cand));
    CL_CHECK("clSetKernelArg(5)", cl_api.SetKernelArg(ctx.kernel, 5, sizeof(int), &n));
    CL_CHECK("clSetKernelArg(6)", cl_api.SetKernelArg(ctx.kernel, 6, sizeof(cl_mem), &ctx.d_res));
    CL_CHECK("clSetKernelArg(8)", cl_api.SetKernelArg(ctx.kernel, 8, sizeof(int), NULL));
    CL_CHECK("clSetKernelArg(9)", cl_api.SetKernelArg(ctx.kernel, 9, sizeof(int), NULL));

    // One work-group per candidate, ctx.local_size work-items each (see opencl_fit_select_device
    // for how that size is chosen and why a launch may still refuse it).
    for (;;) {
        int local_size = ctx.local_size;
        size_t sh_bytes = 7 * local_size * sizeof(double);
        CL_CHECK("clSetKernelArg(7)", cl_api.SetKernelArg(ctx.kernel, 7, sh_bytes, NULL));
        size_t global_size = (size_t)n * local_size;
        size_t local_size_t = (size_t)local_size;
        cl_int e = cl_api.EnqueueNDRangeKernel(ctx.queue, ctx.kernel, 1, NULL, &global_size, &local_size_t, 0, NULL, NULL);
        if (e == CL_SUCCESS) break;
        if (e == CL_INVALID_WORK_GROUP_SIZE && local_size > 8) {
            ctx.local_size = local_size / 2;
            fprintf(stderr, "opencl_fit: this device refuses work-group size %d for the kernel (CL_INVALID_WORK_GROUP_SIZE), using %d\n",
                    local_size, ctx.local_size);
            continue;
        }
        opencl_fail("clEnqueueNDRangeKernel", e);
    }
    CL_CHECK("clFinish", cl_api.Finish(ctx.queue));

    std::vector<CandidateResult> h_res(n);
    CL_CHECK("clEnqueueReadBuffer(results)",
             cl_api.EnqueueReadBuffer(ctx.queue, ctx.d_res, CL_TRUE, 0, n * sizeof(CandidateResult), h_res.data(), 0, NULL, NULL));

    // Same fix as gpu_fit_binary.cu's own gpu_eval_batch() (see that file and
    // kampylos_cpu_patch_pool.h for the full story) -- this loop was the same single-threaded
    // bottleneck for a pathological anchor, now spread across a small worker-thread pool instead.
    // Created once, never destroyed: see kampylos_cpu_patch_pool.h (persistent workers; the
    // thread count depends on whether the vendored VBMicrolensing is thread-safe).
    static KampylosCpuPatchPool& patch_pool = *new KampylosCpuPatchPool();
    patch_pool.complete_batch(h_res, points, ctx.h_data.data(), s, q, chi2, ctx.fs_bound, ctx.flux_offset);
    return chi2;
}

BinaryFitResult opencl_fit_binary_seeds(
    OpenCLFitContext& ctx,
    const std::vector<DataPoint>& data,
    double ln_s, double ln_q,
    const BinarySeedSet& seeds,
    int n_restarts,
    const KampylosBatchedHooks* hooks,
    int cell
) {
    (void)data;
    double s = exp(ln_s), q = exp(ln_q);
    auto eval = [&](const std::vector<Candidate>& batch) { return opencl_eval_batch(ctx, s, q, batch); };
    return kampylos_batched_fit_seeds(eval, ln_s, ln_q, seeds, n_restarts, hooks, cell, stderr);
}

BinaryFitResult opencl_fit_binary_multistart(
    OpenCLFitContext& ctx,
    VBMicrolensing& vbm,
    const std::vector<DataPoint>& data,
    double log_s, double log_q,
    double t0_anchor, double u0_anchor, double tE_anchor,
    double rho_seed,
    int n_alpha_seeds,
    int n_restarts
) {
    if (ctx.n_points != (int)data.size()) opencl_fit_upload_light_curve(ctx, data);
    BinarySeedSet seeds = kampylos_legacy_multistart_seeds(t0_anchor, u0_anchor, tE_anchor, rho_seed, n_alpha_seeds);
    BinaryFitResult out = opencl_fit_binary_seeds(ctx, data, log_s, log_q, seeds, n_restarts);
    std::vector<double> mag(data.size());
    double pr[7] = { log_s, log_q, out.u0, out.alpha, log(out.rho), log(out.tE), out.t0 };
    for (size_t i = 0; i < data.size(); i++) mag[i] = vbm.BinaryLightCurve(pr, data[i].t);
    FluxFit ff = linear_flux_fit(data, mag);
    out.fs = ff.fs; out.fb = ff.fb;
    return out;
}

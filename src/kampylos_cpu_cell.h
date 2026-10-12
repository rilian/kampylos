#pragma once
// The CPU app's per-cell seed loop with a seed-level checkpoint, as a plain function so the
// BOINC wrapper (boinc/main_kampylos_boinc.cpp) only supplies the file and timer plumbing and
// the loop itself can be tested without a BOINC client (tests/test_cpu_resume.cpp).
//
// Why a seed-level checkpoint (2026-10-09, forum threads 72 and 87): a CPU task is often ONE
// grid cell, and one cell can run for many hours (dozens of seeds x a restart-backed simplex
// over thousands of data points). With only a between-cells checkpoint a restart lost
// everything. Seeds are deterministic and the best chi2 wins, so a resumed cell gives exactly
// the uninterrupted result (up to VBMicrolensing's root-solver warm start, see the note in
// kampylos_batched_fit.h: the first evaluations after a resume start the root polish from zero
// instead of the previous roots, a last-bits difference). The checkpoint is only used for the
// cell it names; if it belongs to an older cell, the cell is simply redone.
//
// Seed-split work units (input token "part i K", CPU app v12): this part fits only the seeds
// with k % K == i (interleaved, so the K parts cost about the same); the server keeps the lowest
// chi2 per cell over the K results.
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "binary_fit.h"

struct KampylosSeedCheckpoint {
    int cell = 0;
    size_t next = 0;          // first seed index not yet fitted
    BinaryFitResult best{};   // best fit over the seeds before `next` (chi2 = 1e300 if none)
};

// One line: cell, next, then the ten doubles of BinaryFitResult at %.17g, so they round-trip
// exactly and a resumed cell is bit-identical to an uninterrupted one.
inline std::string kampylos_format_seed_checkpoint(const KampylosSeedCheckpoint& c) {
    const BinaryFitResult& b = c.best;
    char buf[512];
    snprintf(buf, sizeof(buf), "%d %lu %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g\n", c.cell,
             (unsigned long)c.next, b.log_s, b.log_q, b.t0, b.u0, b.tE, b.alpha, b.rho, b.fs, b.fb, b.chi2);
    return buf;
}

inline bool kampylos_parse_seed_checkpoint(const std::string& text, KampylosSeedCheckpoint* c) {
    unsigned long next = 0;
    BinaryFitResult& b = c->best;
    bool ok = sscanf(text.c_str(), "%d %lu %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf", &c->cell, &next, &b.log_s, &b.log_q,
                     &b.t0, &b.u0, &b.tE, &b.alpha, &b.rho, &b.fs, &b.fb, &b.chi2) == 12;
    c->next = next;
    return ok;
}

// Everything the loop needs from its host. The BOINC wrapper maps these onto the BOINC API; the
// unit test maps them onto memory and a "stop after seed k" switch that stands in for a kill.
struct KampylosCellHooks {
    std::function<bool(KampylosSeedCheckpoint&)> read_checkpoint;          // false = none on disk
    std::function<void(const KampylosSeedCheckpoint&)> write_checkpoint;
    std::function<bool()> time_to_checkpoint;                               // BOINC: boinc_time_to_checkpoint()
    std::function<void()> checkpoint_completed;                             // BOINC: boinc_checkpoint_completed()
    std::function<void(double)> progress;                                   // fraction of THIS cell's own seeds done
    std::function<bool()> keep_going;                                       // false = leave now (as a kill would)
};

// Fits one cell's seed set with the seed-level checkpoint. ln_s/ln_q are natural logs.
// *completed is set to false only when keep_going() asked to stop before the last seed.
inline BinaryFitResult kampylos_fit_cell_resumable(
    VBMicrolensing& vbm, const std::vector<DataPoint>& data,
    double ln_s, double ln_q, const BinarySeedSet& seeds,
    int cell, int part, int nparts,
    const KampylosCellHooks& h, FILE* log = nullptr, bool* completed = nullptr
) {
    const size_t n = seeds.x0.size();
    BinaryFitResult best{};
    best.chi2 = 1e300;
    size_t k0 = 0;
    KampylosSeedCheckpoint cp;
    if (h.read_checkpoint && h.read_checkpoint(cp) && cp.cell == cell && cp.next <= n) {
        best = cp.best;
        k0 = cp.next;
        if (log) fprintf(log, "resuming cell %d at seed %lu of %lu\n", cell, (unsigned long)k0, (unsigned long)n);
    }
    const size_t K = (size_t)(nparts > 0 ? nparts : 1), P = (size_t)part;
    size_t own_total = 0, own_before = 0;
    for (size_t k = 0; k < n; k++) if (k % K == P) { own_total++; if (k < k0) own_before++; }
    size_t own_done = own_before;
    if (completed) *completed = true;
    for (size_t k = k0; k < n; k++) {
        if (k % K != P) continue;
        BinaryFitResult r = fit_binary_local_steps(vbm, data, ln_s, ln_q, seeds.x0[k], seeds.step[k]);
        if (r.chi2 < best.chi2) best = r;
        own_done++;
        if (h.progress) h.progress((double)own_done / (double)(own_total ? own_total : 1));
        if (h.time_to_checkpoint && h.time_to_checkpoint()) {
            if (h.write_checkpoint) h.write_checkpoint(KampylosSeedCheckpoint{ cell, k + 1, best });
            if (h.checkpoint_completed) h.checkpoint_completed();
        }
        if (h.keep_going && !h.keep_going()) {
            if (completed) *completed = false;
            return best;
        }
    }
    return best;
}

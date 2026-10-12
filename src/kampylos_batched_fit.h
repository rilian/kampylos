#pragma once
// Backend-independent batched multistart driver, shared by the CUDA (gpu_fit_binary.cu),
// OpenCL (kampylos_opencl_fit.cpp) and Metal (kampylos_metal_fit.mm) paths -- they used to carry
// three copies of this loop. Each backend only supplies `eval`, a callable that takes a batch of
// candidates (all at the same grid cell) and returns one chi2 per candidate, same order.
//
// Same algorithm as the CPU path (binary_fit.h's fit_binary_local_steps over a BinarySeedSet):
// one Nelder-Mead pass per seed (max_iter 400, ftol 1e-9, xtol 1e-8), then up to n_restarts
// restart passes from the converged point with steps {0.3, 0.02, 0.08, 0.15, 0.2}
// (ftol 1e-10, xtol 1e-9). As on the CPU, a seed only gets another restart while its previous
// restart improved it (2026-10-08: the GPU drivers used to restart every seed n_restarts times
// unconditionally, which cost extra GPU rounds and made CPU/GPU cell-by-cell comparisons
// needlessly loose).
//
// CHECKPOINTS (2026-10-10): a GPU cell is one call of kampylos_batched_fit_seeds() and can run
// for an hour or more (hundreds of seeds, ~3000 lockstep rounds), while the BOINC wrappers only
// checkpointed between cells -- a restart redid the whole cell and progress sat at 0 % until it
// was done. The driver now takes optional KampylosBatchedHooks: after every round it reports
// progress and, when the host asks, writes its complete state (phase, per-seed results, the
// active set and every live NelderMeadStepper) as text; on the next call for the same cell it
// resumes from that state. Seeds and rounds are deterministic for a given backend, so the
// resumed cell is bit-identical to an uninterrupted one (tests/test_batched_resume.cpp) -- up
// to one caveat in the completion step: VBMicrolensing's BinaryMag0 starts its root solver from
// the previous call's roots (cmplx_roots_gen(..., use_roots_as_starting_points = true), a
// thread_local array), so a fresh process's instances can return a magnification that differs
// in its last bits from the instance with the full history, and Nelder-Mead can then take a
// slightly different path. Measured (tests/test_boinc_resume.sh, Metal, three runs of 8-9
// in-cell resumes each): the first cell identical every time, the second cell's rho different
// by 0.03-0.2 % every time, chi2 identical to four decimals -- the same chi2 valley.
#include <vector>
#include <string>
#include <sstream>
#include <functional>
#include <cstdio>
#include <cmath>
#include "nelder_mead_stepper.h"
#include "kampylos_gpu_types.h"
#include "binary_fit.h"

static inline Candidate kampylos_to_candidate(const std::vector<double>& x) {
    Candidate c;
    c.t0 = x[0]; c.u0 = x[1]; c.log_tE = x[2]; c.alpha = x[3]; c.log_rho = x[4];
    return c;
}

// ---- checkpoint plumbing -----------------------------------------------------------------------
// Everything the driver needs from its host (the BOINC wrappers map these onto the BOINC API,
// the unit test onto memory and a "stop after round k" switch standing in for a kill).
struct KampylosBatchedHooks {
    std::function<bool(std::string&)> read_checkpoint;          // false = none on disk
    std::function<void(const std::string&)> write_checkpoint;
    std::function<bool()> time_to_checkpoint;                   // BOINC: boinc_time_to_checkpoint()
    std::function<void()> checkpoint_completed;                 // BOINC: boinc_checkpoint_completed()
    std::function<void(double)> progress;                       // fraction of THIS cell done, 0..1
    std::function<bool()> keep_going;                           // false = leave now (as a kill would)
};

// The driver's state between two rounds. phase 0 is the initial pass over every seed, phases
// 1..n_restarts the restart passes; `steppers` are the searches of the pass in progress and
// idx[k] is the seed stepper k belongs to.
struct KampylosBatchedState {
    int cell = 0, n_seeds = 0, n_restarts = 0, phase = 0;
    long round = 0;
    std::vector<SimplexResultLite> current;   // best result per seed so far (empty during phase 0)
    std::vector<int> active;                  // per seed: still worth restarting
    std::vector<int> idx;
    std::vector<NelderMeadStepper> steppers;
};

namespace kampylos_batched_detail {

inline void put(std::ostringstream& o, double v) { char b[32]; snprintf(b, sizeof(b), "%.17g", v); o << ' ' << b; }
inline void put(std::ostringstream& o, const std::vector<double>& v) { o << ' ' << v.size(); for (double x : v) put(o, x); }
inline void put(std::ostringstream& o, const std::vector<std::vector<double>>& v) { o << ' ' << v.size(); for (const auto& x : v) put(o, x); }
inline bool get(std::istringstream& i, double& v) { return (bool)(i >> v); }
inline bool get(std::istringstream& i, std::vector<double>& v) {
    size_t n; if (!(i >> n) || n > 1000000) return false;
    v.resize(n); for (auto& x : v) if (!get(i, x)) return false;
    return true;
}
inline bool get(std::istringstream& i, std::vector<std::vector<double>>& v) {
    size_t n; if (!(i >> n) || n > 1000000) return false;
    v.resize(n); for (auto& x : v) if (!get(i, x)) return false;
    return true;
}

} // namespace kampylos_batched_detail

// One line per item; every double at %.17g so the state round-trips exactly.
inline std::string kampylos_batched_state_to_text(const KampylosBatchedState& s) {
    using namespace kampylos_batched_detail;
    std::ostringstream o;
    o << "kampylos_batched_fit 1\n";
    o << s.cell << ' ' << s.n_seeds << ' ' << s.n_restarts << ' ' << s.phase << ' ' << s.round << '\n';
    o << s.current.size();
    for (const auto& r : s.current) { o << ' ' << (r.converged ? 1 : 0) << ' ' << r.iterations; put(o, r.fval); put(o, r.x); }
    o << '\n' << s.active.size();
    for (int a : s.active) o << ' ' << a;
    o << '\n' << s.idx.size();
    for (int k : s.idx) o << ' ' << k;
    o << '\n' << s.steppers.size() << '\n';
    for (const auto& st : s.steppers) {
        NelderMeadStepper::State t = st.state();
        o << t.n << ' ' << t.max_iter << ' ' << t.iter << ' ' << t.phase << ' ' << (t.converged ? 1 : 0);
        put(o, t.ftol); put(o, t.xtol); put(o, t.fr);
        put(o, t.simplex); put(o, t.pending); put(o, t.fval);
        put(o, t.centroid); put(o, t.worst); put(o, t.xr); put(o, t.xe); put(o, t.xc);
        o << '\n';
    }
    o << "end\n";
    return o.str();
}

inline bool kampylos_batched_state_from_text(const std::string& text, KampylosBatchedState* s) {
    using namespace kampylos_batched_detail;
    std::istringstream i(text);
    std::string tag; int version;
    if (!(i >> tag >> version) || tag != "kampylos_batched_fit" || version != 1) return false;
    KampylosBatchedState t;
    if (!(i >> t.cell >> t.n_seeds >> t.n_restarts >> t.phase >> t.round)) return false;
    size_t n;
    if (!(i >> n) || n > 100000) return false;
    t.current.resize(n);
    for (auto& r : t.current) {
        int conv;
        if (!(i >> conv >> r.iterations) || !get(i, r.fval) || !get(i, r.x)) return false;
        r.converged = conv != 0;
    }
    if (!(i >> n) || n > 100000) return false;
    t.active.resize(n);
    for (auto& a : t.active) if (!(i >> a)) return false;
    if (!(i >> n) || n > 100000) return false;
    t.idx.resize(n);
    for (auto& k : t.idx) if (!(i >> k)) return false;
    if (!(i >> n) || n > 100000) return false;
    t.steppers.reserve(n);
    for (size_t k = 0; k < n; k++) {
        NelderMeadStepper::State st;
        int conv;
        if (!(i >> st.n >> st.max_iter >> st.iter >> st.phase >> conv)) return false;
        st.converged = conv != 0;
        if (!get(i, st.ftol) || !get(i, st.xtol) || !get(i, st.fr)) return false;
        if (!get(i, st.simplex) || !get(i, st.pending) || !get(i, st.fval)) return false;
        if (!get(i, st.centroid) || !get(i, st.worst) || !get(i, st.xr) || !get(i, st.xe) || !get(i, st.xc)) return false;
        if (st.n < 1 || st.n > 32 || (int)st.simplex.size() != st.n + 1 || (int)st.fval.size() != st.n + 1) return false;
        t.steppers.emplace_back(st);
    }
    if (!(i >> tag) || tag != "end") return false;
    if (t.idx.size() != t.steppers.size()) return false;
    *s = std::move(t);
    return true;
}

// ---- the driver ----------------------------------------------------------------------------------
// Advances every live stepper of the pass in progress to completion, one eval() per round.
// Returns false if hooks->keep_going() asked to stop (the state is left exactly between rounds).
template <class Eval>
bool kampylos_batched_run_pass(Eval& eval, KampylosBatchedState& s, const KampylosBatchedHooks* hooks) {
    const int n_steppers = (int)s.steppers.size();
    for (;;) {
        std::vector<Candidate> batch;
        std::vector<int> pending_count(n_steppers, 0);
        bool any_active = false;
        for (int k = 0; k < n_steppers; k++) {
            if (s.steppers[k].done()) continue;
            any_active = true;
            const auto& pts = s.steppers[k].pending_points();
            pending_count[k] = (int)pts.size();
            for (const auto& p : pts) batch.push_back(kampylos_to_candidate(p));
        }
        if (!any_active) return true;

        std::vector<double> chi2 = eval(batch);

        int offset = 0;
        for (int k = 0; k < n_steppers; k++) {
            if (pending_count[k] == 0) continue;
            std::vector<double> sub(chi2.begin() + offset, chi2.begin() + offset + pending_count[k]);
            offset += pending_count[k];
            s.steppers[k].submit_results(sub);
        }
        s.round++;

        if (hooks) {
            if (hooks->progress) {
                // this pass's share: mean over its steppers of iterations done / allowed
                double f = 0;
                for (const auto& st : s.steppers)
                    f += st.done() ? 1.0 : std::min(1.0, (double)st.iterations() / std::max(1, st.max_iterations()));
                f /= std::max(1, n_steppers);
                hooks->progress((s.phase + f) / (double)(s.n_restarts + 1));
            }
            if (hooks->time_to_checkpoint && hooks->time_to_checkpoint()) {
                if (hooks->write_checkpoint) hooks->write_checkpoint(kampylos_batched_state_to_text(s));
                if (hooks->checkpoint_completed) hooks->checkpoint_completed();
            }
            if (hooks->keep_going && !hooks->keep_going()) return false;
        }
    }
}

// Drives N independent NelderMeadStepper instances to completion, batching every round's
// pending points across all of them into one eval() call. (Kept for callers that want a plain
// batch without the restart logic or checkpoints.)
template <class Eval>
std::vector<SimplexResultLite> kampylos_batched_nelder_mead(
    Eval& eval,
    const std::vector<std::vector<double>>& x0s,
    const std::vector<std::vector<double>>& steps,
    int max_iter, double ftol, double xtol
) {
    KampylosBatchedState s;
    s.steppers.reserve(x0s.size());
    for (size_t i = 0; i < x0s.size(); i++) { s.steppers.emplace_back(x0s[i], steps[i], max_iter, ftol, xtol); s.idx.push_back((int)i); }
    kampylos_batched_run_pass(eval, s, nullptr);
    std::vector<SimplexResultLite> out;
    out.reserve(s.steppers.size());
    for (auto& st : s.steppers) out.push_back(st.result());
    return out;
}

// Runs a whole seed set at one grid cell and returns the best parameter vector (log_s/log_q
// are set to the given natural logs; fs/fb are left 0 -- the caller finalises on the host).
// hooks/cell: optional checkpointing and progress (see the top comment); a checkpoint is only
// used if it names this cell and the same number of seeds and restarts. *completed is set to
// false only when hooks->keep_going() asked to stop before the cell was done.
template <class Eval>
BinaryFitResult kampylos_batched_fit_seeds(Eval& eval, double ln_s, double ln_q,
                                           const BinarySeedSet& seeds, int n_restarts = 2,
                                           const KampylosBatchedHooks* hooks = nullptr, int cell = 0,
                                           FILE* log = nullptr, bool* completed = nullptr) {
    const int n = (int)seeds.size();
    KampylosBatchedState s;
    bool resumed = false;
    if (hooks && hooks->read_checkpoint) {
        std::string text;
        KampylosBatchedState cp;
        if (hooks->read_checkpoint(text) && kampylos_batched_state_from_text(text, &cp) &&
            cp.cell == cell && cp.n_seeds == n && cp.n_restarts == n_restarts) {
            s = std::move(cp);
            resumed = true;
            if (log) fprintf(log, "resuming cell %d at pass %d, round %ld (%zu searches live)\n", cell, s.phase, s.round, s.steppers.size());
        }
    }
    if (!resumed) {
        s.cell = cell; s.n_seeds = n; s.n_restarts = n_restarts; s.phase = 0;
        s.active.assign(n, 1);
        s.steppers.reserve(n);
        for (int i = 0; i < n; i++) { s.steppers.emplace_back(seeds.x0[i], seeds.step[i], 400, 1e-9, 1e-8); s.idx.push_back(i); }
    }
    if (completed) *completed = true;

    for (;;) {
        if (!kampylos_batched_run_pass(eval, s, hooks)) {
            if (completed) *completed = false;
            BinaryFitResult none{};
            none.chi2 = 1e300;
            return none;
        }
        std::vector<SimplexResultLite> res;
        res.reserve(s.steppers.size());
        for (auto& st : s.steppers) res.push_back(st.result());
        if (s.phase == 0) {
            s.current = res;
        } else {
            for (size_t k = 0; k < s.idx.size(); k++) {
                int i = s.idx[k];
                if (res[k].fval < s.current[i].fval) s.current[i] = res[k];
                else s.active[i] = 0;
            }
        }
        if (s.phase >= s.n_restarts) break;
        s.phase++;
        s.idx.clear();
        s.steppers.clear();
        for (int i = 0; i < n; i++) {
            if (!s.active[i]) continue;
            s.idx.push_back(i);
            s.steppers.emplace_back(s.current[i].x, std::vector<double>{ 0.3, 0.02, 0.08, 0.15, 0.2 }, 400, 1e-10, 1e-9);
        }
        if (s.idx.empty()) break;
    }

    int best = 0;
    for (size_t i = 1; i < s.current.size(); i++) if (s.current[i].fval < s.current[best].fval) best = (int)i;

    BinaryFitResult out;
    out.log_s = ln_s; out.log_q = ln_q;
    out.t0 = s.current[best].x[0]; out.u0 = s.current[best].x[1]; out.tE = exp(s.current[best].x[2]);
    out.alpha = s.current[best].x[3]; out.rho = exp(s.current[best].x[4]);
    out.chi2 = s.current[best].fval;
    out.fs = 0; out.fb = 0;
    return out;
}

// The 96-seed multistart around one anchor (legacy fit_binary_multistart seeds), as a seed set.
inline BinarySeedSet kampylos_legacy_multistart_seeds(double t0_anchor, double u0_anchor, double tE_anchor,
                                                      double rho_seed = 1e-3, int n_alpha_seeds = 8) {
    BinarySeedSet set;
    const double rho_seeds[] = { rho_seed, 1e-4, 1e-2 };
    const double u0_mag_seeds[] = { std::fabs(u0_anchor), 0.02 };
    for (int i = 0; i < n_alpha_seeds; i++) {
        double alpha_seed = 2.0 * M_PI * i / n_alpha_seeds;
        for (double u0_mag : u0_mag_seeds)
            for (double u0_sign : { 1.0, -1.0 })
                for (double rs : rho_seeds)
                    set.add({ t0_anchor, u0_sign * u0_mag, log(tE_anchor), alpha_seed, log(rs) },
                            { std::max(0.5, tE_anchor * 0.05), 0.05, 0.2, 0.3, 0.5 });
    }
    return set;
}

// Flux-centring used by every GPU upload: the weighted mean flux. Subtracting a constant from
// all fluxes changes nothing but fb (fs, chi2 identical) and removes most of the cancellation
// in the chi2 = S_yy - fs*S_Ay - fb*S_1y identity -- essential for Metal's float sums.
inline double kampylos_flux_offset(const std::vector<DataPoint>& data) {
    double sw = 0, swy = 0;
    for (const auto& d : data) { double w = 1.0 / (d.sigma * d.sigma); sw += w; swy += w * d.flux; }
    return sw > 0 ? swy / sw : 0.0;
}

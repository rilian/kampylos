#pragma once
// Work-unit plumbing shared by every Kampylos backend (CPU main_kampylos_boinc.cpp, CUDA
// main_kampylos_gpu_boinc.cpp, OpenCL main_kampylos_opencl_boinc.cpp, Metal
// main_kampylos_metal_boinc.cpp, and the standalone fit_event CLI used by the regression
// tests): input-line parsing with the grid-convention marker, the single-lens anchor stage,
// seeding, per-cell host-side finalisation (exact chi2 + few-points diagnostics) and the
// result-file format. Keeping this in one header is what guarantees the four apps write
// byte-compatible result files that the server can parse with one parser.
//
// ---- Input ("in" file), one line ---------------------------------------------------------
//   <mag|flux> <s_min> <s_max> <n_s> <q_min> <q_max> <n_q> [log10|ln] [part <i> <K>]
// The optional 8th token is the GRID CONVENTION MARKER (added 2026-10-08):
//   * "log10": the grid values are log10(s), log10(q).
//   * absent (every WU created before 2026-10-08) or "ln": natural logs -- the historical
//     behaviour, kept so the thousands of already-queued WUs mean what they always meant.
// Any other trailing token is rejected (better a hard error than silently guessing).
// The optional trailing "part <i> <K>" (added 2026-10-10, CPU app v12) splits every cell's seed set
// over K work units: part i fits only the seeds whose index k satisfies k % K == i (interleaved, so
// the parts cost about the same). Each part writes the normal one-line-per-cell result (best of ITS
// seeds); the result files of the K parts are merged by keeping the lowest chi2 per cell, which is
// exactly what kampylos_analyze.py's dedupe_cells() already did for re-issued work units. Absent =
// part 0 of 1 = all seeds, the historical behaviour. Backends that cannot split (GPU) must reject
// nparts > 1.
// NOTE: binaries built before this change read the line with a fixed 7-field fscanf and
// IGNORE the marker, i.e. they would run a log10 WU with ln semantics. Their result header is
// the old one ("# log_s log_q chi2 ..."), which kampylos_validate.sh rejects for a log10 WU,
// so such a result can never be assimilated as log10 data.
//
// ---- Output ("out" file) -------------------------------------------------------------------
//   # grid=log10 log10_s log10_q chi2 t0 u0 tE alpha rho fs fb dchi2_single max_pt_dchi2 top3_dchi2 n_pts50 n_nights50 n_nights5pct
//   # anchor format=2 backend=<cpu|cuda|opencl|metal> npts=... nights=... pspl_chi2=... ...
//   <one line per grid cell, 16 fields>
// (with grid=ln the first two column names are ln_s ln_q). The pre-2026-10-08 format was a
// single "# log_s log_q chi2 t0 u0 tE alpha rho fs fb" header and 10 fields per line; the
// server keeps those files apart (see server/kampylos_assimilate.sh).
//
// Per-cell diagnostic columns (bug fix 5, all relative to the single-lens anchor = best of
// PSPL/FSPL/FSPL+LD, per-point d_i = chi2_anchor,i - chi2_binary,i, so sum d_i = dchi2_single):
//   dchi2_single  chi2(anchor) - chi2(this cell); > 0 means the binary fits better
//   max_pt_dchi2  largest single-point d_i
//   top3_dchi2    sum of the three largest d_i (dchi2_single - top3_dchi2 is the improvement
//                 that survives dropping the three most influential points)
//   n_pts50       minimum number of points whose d_i add up to >= 50% of dchi2_single
//                 (0 if dchi2_single <= 0)
//   n_nights50    same, counting whole nights (night = time cluster, see night_index())
//   n_nights5pct  number of nights each contributing >= 5% of dchi2_single
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include "lightcurve.h"
#include "single_lens.h"
#include "pspl_prefit.h"
#include "binary_fit.h"

#define KAMPYLOS_RESULT_FORMAT 2

struct KampylosWuInput {
    bool is_mag = true;
    double s_min = 0, s_max = 0, q_min = 0, q_max = 0;
    int n_s = 1, n_q = 1;
    bool log10_grid = false;   // false = natural log (legacy)
    int part = 0, nparts = 1;  // seed split, see the "part" token above
};

inline const char* kampylos_grid_name(bool log10_grid) { return log10_grid ? "log10" : "ln"; }

// Grid value -> natural log, the convention VBMicrolensing (and every fit function) uses.
inline double kampylos_grid_to_ln(double v, bool log10_grid) {
    return log10_grid ? v * 2.302585092994045684 : v;
}

inline double kampylos_grid_value(double lo, double hi, int n, int i) {
    return (n == 1) ? lo : lo + (hi - lo) * i / (n - 1);
}

// Parses the whole "in" file content. Returns false with *err set on any problem.
inline bool kampylos_parse_input(const std::string& text, KampylosWuInput* in, std::string* err) {
    std::vector<std::string> tok;
    {
        size_t i = 0;
        while (i < text.size()) {
            while (i < text.size() && isspace((unsigned char)text[i])) i++;
            size_t j = i;
            while (j < text.size() && !isspace((unsigned char)text[j])) j++;
            if (j > i) tok.push_back(text.substr(i, j - i));
            i = j;
        }
    }
    // optional trailing "part <i> <K>"
    in->part = 0; in->nparts = 1;
    if (tok.size() >= 10 && tok[tok.size() - 3] == "part") {
        char* pe = nullptr;
        long pi = strtol(tok[tok.size() - 2].c_str(), &pe, 10);
        if (!pe || *pe) { *err = "bad part index '" + tok[tok.size() - 2] + "'"; return false; }
        long pk = strtol(tok[tok.size() - 1].c_str(), &pe, 10);
        if (!pe || *pe || pk < 1 || pk > 64 || pi < 0 || pi >= pk) { *err = "bad part " + tok[tok.size() - 2] + " of " + tok[tok.size() - 1]; return false; }
        in->part = (int)pi; in->nparts = (int)pk;
        tok.resize(tok.size() - 3);
    }
    if (tok.size() != 7 && tok.size() != 8) {
        *err = "expected: mag|flux s_min s_max n_s q_min q_max n_q [log10|ln] [part i K], got " + std::to_string(tok.size()) + " tokens";
        return false;
    }
    if (tok[0] != "mag" && tok[0] != "flux") { *err = "first token must be mag or flux, got '" + tok[0] + "'"; return false; }
    in->is_mag = (tok[0] == "mag");
    char* end = nullptr;
    double v[6];
    const int idx[6] = { 1, 2, 4, 5, 3, 6 };
    for (int k = 0; k < 6; k++) {
        v[k] = strtod(tok[idx[k]].c_str(), &end);
        if (!end || *end || !std::isfinite(v[k])) { *err = "bad number '" + tok[idx[k]] + "'"; return false; }
    }
    in->s_min = v[0]; in->s_max = v[1]; in->q_min = v[2]; in->q_max = v[3];
    in->n_s = (int)v[4]; in->n_q = (int)v[5];
    if (in->n_s < 1 || in->n_q < 1 || in->n_s != v[4] || in->n_q != v[5] || in->n_s * in->n_q > 10000) {
        *err = "bad grid size"; return false;
    }
    in->log10_grid = false;
    if (tok.size() == 8) {
        if (tok[7] == "log10") in->log10_grid = true;
        else if (tok[7] == "ln") in->log10_grid = false;
        else { *err = "unknown grid marker '" + tok[7] + "' (expected log10 or ln)"; return false; }
    }
    return true;
}

inline bool kampylos_read_input_file(FILE* f, KampylosWuInput* in, std::string* err) {
    std::string text;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        text.append(buf, n);
        if (text.size() > 4096) { *err = "input file too long"; return false; }
    }
    return kampylos_parse_input(text, in, err);
}

// ---- single-lens stage ---------------------------------------------------------------------
struct KampylosWuAnchor {
    SingleLensAnchor sl;
    std::vector<int> night;     // night index per data point
    int n_nights = 0;
    BinarySeedAnchors seeds;    // anchors for kampylos_make_seeds()
};

inline KampylosWuAnchor kampylos_compute_anchor(const std::vector<DataPoint>& data) {
    KampylosWuAnchor a;
    a.sl = fit_single_lens_anchor(data);
    a.night = night_index(data, &a.n_nights);
    double t0_raw, tE_raw;
    raw_peak_anchor(data, kampylos_sl_detail::robust_peak_time(data), &t0_raw, &tE_raw);
    a.sl.t0_raw = t0_raw; a.sl.tE_raw = tE_raw;
    const SingleLensFit& b = a.sl.best;
    // The finite-source size is only used for seeding when the data actually measure it (FSPL
    // beats PSPL by > 25 in chi2). An unconstrained rho can wander to ~0.1 (degenerate with u0)
    // and would start the binary search in the regime where most points need VBMicrolensing's
    // contour integration -- many times slower per evaluation, for no benefit.
    bool rho_measured = (a.sl.pspl.chi2 - a.sl.fspl.chi2) > 25.0;
    a.seeds.t0 = b.t0; a.seeds.u0 = std::max(b.u0, 1e-4); a.seeds.tE = b.tE;
    a.seeds.rho = (b.kind == SL_PSPL || !rho_measured) ? 0.0 : b.rho;
    a.seeds.t0_raw = t0_raw; a.seeds.tE_raw = tE_raw;
    // The binary model uses a uniform source, so it reproduces the uniform FSPL fit exactly (or
    // the PSPL fit with a point-like source when rho is not measured).
    const SingleLensFit& u = rho_measured ? a.sl.fspl : a.sl.pspl;
    a.seeds.sl_t0 = u.t0; a.seeds.sl_u0 = std::max(u.u0, 1e-4); a.seeds.sl_tE = u.tE;
    a.seeds.sl_rho = rho_measured ? u.rho : 1e-4;
    for (const auto& alt : a.sl.pspl_alt) a.seeds.alt.push_back({ alt.t0, alt.u0, alt.tE });
    return a;
}

static inline const char* kampylos_sl_kind_name(int k) {
    return k == SL_PSPL ? "pspl" : (k == SL_FSPL ? "fspl" : "fspl_ld");
}

inline void kampylos_write_header(FILE* f, const KampylosWuInput& in, const KampylosWuAnchor& a, const char* backend) {
    const char* g = kampylos_grid_name(in.log10_grid);
    fprintf(f, "# grid=%s %s_s %s_q chi2 t0 u0 tE alpha rho fs fb dchi2_single max_pt_dchi2 top3_dchi2 n_pts50 n_nights50 n_nights5pct\n", g, g, g);
    const SingleLensAnchor& s = a.sl;
    const SingleLensFit& b = s.best;
    fprintf(f, "# anchor format=%d backend=%s npts=%d nights=%d"
               " pspl_chi2=%.4f fspl_chi2=%.4f fspl_ld_chi2=%.4f best=%s chi2=%.4f dof=%d chi2dof=%.5f"
               " worst1pct_frac=%.5f n_peak=%d chi2dof_peak=%.5f chi2dof_base=%.5f"
               " t0=%.6f u0=%.6f tE=%.6f rho=%.6e ld_a1=%.4f fs=%.6g fb=%.6g"
               " pspl_t0=%.6f pspl_u0=%.6f pspl_tE=%.6f fspl_rho=%.6e\n",
            KAMPYLOS_RESULT_FORMAT, backend, s.npts, a.n_nights,
            s.pspl.chi2, s.fspl.chi2, s.fspl_ld.chi2, kampylos_sl_kind_name(b.kind), b.chi2, s.dof, s.chi2dof,
            s.worst1pct_frac, s.n_peak, s.chi2dof_peak, s.chi2dof_base,
            b.t0, b.u0, b.tE, b.rho, b.ld_a1, b.fs, b.fb,
            s.pspl.t0, s.pspl.u0, s.pspl.tE, s.fspl.rho);
}

// ---- per-cell finalisation -------------------------------------------------------------------
struct KampylosCellDiag {
    double dchi2 = 0, max_pt = 0, top3 = 0;
    int n_pts50 = 0, n_nights50 = 0, n_nights5pct = 0;
};

// Recomputes the cell's best-fit model exactly on the host (VBMicrolensing over every point)
// -- the SAME code on every backend, so the reported chi2/fs/fb of a given parameter vector do
// not depend on which GPU produced it -- and derives the few-points diagnostics against the
// anchor. r.log_s/r.log_q must hold NATURAL logs on entry.
inline KampylosCellDiag kampylos_finalize_cell(VBMicrolensing& vbm, const std::vector<DataPoint>& data,
                                               const KampylosWuAnchor& a, BinaryFitResult& r) {
    const size_t n = data.size();
    std::vector<double> mag(n);
    double pr[7] = { r.log_s, r.log_q, r.u0, r.alpha, log(r.rho), log(r.tE), r.t0 };
    bool ok = std::isfinite(r.chi2) && r.chi2 < 1e17;
    for (size_t i = 0; ok && i < n; i++) {
        mag[i] = vbm.BinaryLightCurve(pr, data[i].t);
        if (!std::isfinite(mag[i]) || mag[i] <= 0) ok = false;
    }
    KampylosCellDiag d;
    if (!ok) { r.chi2 = 1e18; r.fs = 0; r.fb = 0; return d; }
    FluxFit ff = linear_flux_fit(data, mag);
    r.fs = ff.fs; r.fb = ff.fb; r.chi2 = ff.chi2;

    std::vector<double> di(n);
    double total = 0;
    for (size_t i = 0; i < n; i++) {
        double rb = (data[i].flux - (ff.fs * mag[i] + ff.fb)) / data[i].sigma;
        di[i] = a.sl.chi2_pt[i] - rb * rb;
        total += di[i];
    }
    d.dchi2 = a.sl.best.chi2 - ff.chi2;   // == total up to rounding
    std::vector<double> sorted = di;
    std::sort(sorted.begin(), sorted.end(), std::greater<double>());
    d.max_pt = n ? sorted[0] : 0;
    for (size_t i = 0; i < 3 && i < n; i++) d.top3 += std::max(sorted[i], 0.0);
    if (d.dchi2 > 0) {
        double acc = 0;
        for (size_t i = 0; i < n; i++) { acc += sorted[i]; if (acc >= 0.5 * d.dchi2) { d.n_pts50 = (int)i + 1; break; } }
        std::vector<double> per_night(a.n_nights, 0.0);
        for (size_t i = 0; i < n; i++) per_night[a.night[i]] += di[i];
        std::sort(per_night.begin(), per_night.end(), std::greater<double>());
        acc = 0;
        for (size_t k = 0; k < per_night.size(); k++) { acc += per_night[k]; if (acc >= 0.5 * d.dchi2) { d.n_nights50 = (int)k + 1; break; } }
        for (double v : per_night) if (v >= 0.05 * d.dchi2) d.n_nights5pct++;
    }
    return d;
}

// grid_s/grid_q: the cell's coordinates in the WU's own grid convention.
inline void kampylos_write_cell(FILE* f, double grid_s, double grid_q, const BinaryFitResult& r, const KampylosCellDiag& d) {
    fprintf(f, "%.6f %.6f %.4f %.6f %.6f %.6f %.6f %.6e %.6g %.6g %.4f %.4f %.4f %d %d %d\n",
            grid_s, grid_q, r.chi2, r.t0, r.u0, r.tE, r.alpha, r.rho, r.fs, r.fb,
            d.dchi2, d.max_pt, d.top3, d.n_pts50, d.n_nights50, d.n_nights5pct);
}

inline void kampylos_log_anchor(FILE* f, const KampylosWuAnchor& a) {
    const SingleLensAnchor& s = a.sl;
    fprintf(f, "Single-lens anchor: PSPL chi2=%.3f FSPL chi2=%.3f FSPL+LD chi2=%.3f -> best=%s t0=%.4f u0=%.5f tE=%.4f rho=%.3e a1=%.3f chi2/dof=%.3f\n",
            s.pspl.chi2, s.fspl.chi2, s.fspl_ld.chi2, kampylos_sl_kind_name(s.best.kind),
            s.best.t0, s.best.u0, s.best.tE, s.best.rho, s.best.ld_a1, s.chi2dof);
    for (const auto& alt : s.pspl_alt)
        fprintf(f, "  fixed-tE PSPL profile point (extra binary seeds): t0=%.4f u0=%.5f tE=%.4f chi2=%.3f\n", alt.t0, alt.u0, alt.tE, alt.chi2);
}

// ---- the per-WU cell loop, shared by every backend ------------------------------------------
// fit_cell(ln_s, ln_q, seeds) -> BinaryFitResult runs the backend's search for one cell;
// after_cell(cells_done, total) is called after each cell's line has been written and flushed
// (BOINC progress/checkpointing). Cells are row-major over (is, iq), [start_cell, end_cell).
template <class FitCell, class AfterCell>
void kampylos_run_cells(FILE* fout, FILE* log, const KampylosWuInput& in, const std::vector<DataPoint>& data,
                        const KampylosWuAnchor& a, VBMicrolensing& vbm, int start_cell, int end_cell,
                        FitCell fit_cell, AfterCell after_cell) {
    int total = in.n_s * in.n_q;
    for (int cell = start_cell; cell < end_cell && cell < total; cell++) {
        int is = cell / in.n_q, iq = cell % in.n_q;
        double gs = kampylos_grid_value(in.s_min, in.s_max, in.n_s, is);
        double gq = kampylos_grid_value(in.q_min, in.q_max, in.n_q, iq);
        double ln_s = kampylos_grid_to_ln(gs, in.log10_grid), ln_q = kampylos_grid_to_ln(gq, in.log10_grid);
        BinarySeedSet seeds = kampylos_make_seeds(a.seeds, exp(ln_s), exp(ln_q));
        BinaryFitResult r = fit_cell(ln_s, ln_q, seeds);
        r.log_s = ln_s; r.log_q = ln_q;
        KampylosCellDiag d = kampylos_finalize_cell(vbm, data, a, r);
        kampylos_write_cell(fout, gs, gq, r, d);
        fflush(fout);
        after_cell(cell + 1, total);
        if (log) fprintf(log, "[%d/%d] %s_s=%.4f %s_q=%.4f chi2=%.3f dchi2_single=%.3f n_pts50=%d\n",
                         cell + 1, total, kampylos_grid_name(in.log10_grid), gs, kampylos_grid_name(in.log10_grid), gq,
                         r.chi2, d.dchi2, d.n_pts50);
    }
}

// Splits an existing result file into its header lines plus the first `keep_cells` complete
// data lines (keep_cells < 0 = all of them). A trailing line without '\n' is a torn write from
// an interrupted run and is dropped. Returns the number of complete data lines kept, or -1 if
// the file cannot be read or does not start with the format-2 header.
// whole_out, if given, receives the file as it is on disk.
inline int kampylos_split_output(const char* path, int keep_cells, std::string* kept_out, std::string* whole_out = nullptr) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    std::string content, kept;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) content.append(buf, n);
    fclose(f);
    if (whole_out) *whole_out = content;
    if (content.compare(0, 7, "# grid=") != 0) return -1;
    size_t pos = 0;
    int data_lines = 0;
    while (pos < content.size()) {
        size_t eol = content.find('\n', pos);
        if (eol == std::string::npos) break;           // drop a torn last line
        std::string line = content.substr(pos, eol - pos + 1);
        if (line[0] == '#') kept += line;
        else if (keep_cells < 0 || data_lines < keep_cells) { kept += line; data_lines++; }
        pos = eol + 1;
    }
    if (kept_out) *kept_out = kept;
    return data_lines;
}

// Number of complete cell lines in a result file (-1: no usable file). The result file is the
// authoritative record of finished cells: a line is only ever written after its cell completed,
// so a resume can trust it even when the checkpoint file is missing or stale.
inline int kampylos_count_complete_cells(const char* path) {
    return kampylos_split_output(path, -1, nullptr);
}

// On a resumed run, cut the output file back to its header lines plus the first `keep_cells`
// data lines (in practice: drop the torn last line of a run killed mid-write). Returns false if
// the file cannot be read back, does not start with the format-2 header, or holds fewer than
// keep_cells complete lines (the caller then restarts the WU from cell 0).
// A file that needs no cutting is left untouched, and one that does is replaced through a
// temporary file and a rename: rewriting "out" in place meant a kill during the rewrite left an
// empty file, and the next start then redid every cell. rename() cannot replace an existing
// file on Windows; there the old file is removed first (BOINC's own boinc_rename does the same).
inline bool kampylos_trim_output(const char* path, int keep_cells) {
    std::string kept, whole;
    int data_lines = kampylos_split_output(path, keep_cells, &kept, &whole);
    if (data_lines < keep_cells) return false;
    if (kept == whole) return true;
    std::string tmp = std::string(path) + ".trim";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(kept.data(), 1, kept.size(), f) == kept.size();
    ok = (fclose(f) == 0) && ok;
    if (!ok) { remove(tmp.c_str()); return false; }
#ifdef _WIN32
    remove(path);
#endif
    return rename(tmp.c_str(), path) == 0;
}

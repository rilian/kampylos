#pragma once
// BOINC plumbing shared by the four Kampylos app mains (CPU main_kampylos_boinc.cpp, CUDA
// main_kampylos_gpu_boinc.cpp, OpenCL main_kampylos_opencl_boinc.cpp, Metal
// main_kampylos_metal_boinc.cpp) -- input parsing, photometry loading, the single-lens anchor,
// output header/resume handling, checkpointing. The only per-backend code left in each main is
// device setup and the one call that fits a grid cell, so the four apps cannot drift apart in
// file format or grid convention again (the ln/log10 bug was in all four copies).
//
// INCLUDE ORDER: the including main must include vendor/kampylos/src/kampylos_wu.h (and its
// backend header) BEFORE this file: boinc_api.h drags in windows.h on Windows, whose macros
// (e.g. "scr2") collide with names inside VBMicrolensingLibrary.h -- see the note in
// main_kampylos_boinc.cpp.
//
// CHECKPOINTS (2026-10-10 rework). Two files in the slot directory:
//   kampylos_checkpoint       one integer, the number of grid cells already completed and
//                             written to "out" (row-major over (is, iq));
//   kampylos_seed_checkpoint  the CPU app's progress inside the current cell (see
//                             src/kampylos_cpu_cell.h).
// Both are written to a temporary file and renamed into place, so a kill mid-write leaves the
// previous checkpoint intact. The cell checkpoint is written after EVERY cell, inside a BOINC
// critical section, not only when boinc_time_to_checkpoint() happens to be true: the CPU app's
// seed loop consumes that flag after almost every seed, so the cell checkpoint used to be
// skipped at most cell boundaries, and the next restart then threw away a finished cell.
// On resume the result file is the authoritative record of finished cells (a line is only ever
// written after its cell completed; a torn last line is dropped): the WU resumes from the
// number of complete lines in "out", whatever the checkpoint file says, and never truncates an
// output that still has good lines -- a lost, stale or not-yet-updated checkpoint file costs
// nothing. (Until 2026-10-12 it resumed from min(checkpoint, complete lines): a kill in the
// gap between a cell's line reaching "out" and its checkpoint write then redid that whole cell,
// hours of work, for nothing.)
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include "../src/kampylos_cpu_cell.h"   // seed checkpoint format (Kampylos headers first, see above)
#include "../src/kampylos_batched_fit.h" // KampylosBatchedHooks (GPU apps' in-cell checkpoint)
#include "boinc_api.h"
#include "filesys.h"

static const char *KAMPYLOS_CHECKPOINT_NAME = "kampylos_checkpoint";
static const char *KAMPYLOS_SEED_CHECKPOINT_NAME = "kampylos_seed_checkpoint";

// Writes `content` to the resolved `name` atomically (temporary file + rename). Returns false if
// the file could not be written, so the caller does not report a checkpoint that does not exist.
static bool kampylos_write_file_atomic(const char *name, const std::string &content) {
    char path[512];
    if (boinc_resolve_filename(name, path, sizeof(path))) return false;
    std::string tmp = std::string(path) + ".tmp";
    FILE *f = boinc_fopen(tmp.c_str(), "w");
    if (!f) return false;
    bool ok = fwrite(content.data(), 1, content.size(), f) == content.size();
    ok = (fclose(f) == 0) && ok;
    if (!ok) return false;
    return boinc_rename(tmp.c_str(), path) == 0;
}

// Cells completed according to the checkpoint file; -1 if there is none (or it is unreadable).
static int kampylos_read_checkpoint() {
    char path[512];
    if (boinc_resolve_filename(KAMPYLOS_CHECKPOINT_NAME, path, sizeof(path))) return -1;
    FILE *f = boinc_fopen(path, "r");
    if (!f) return -1;
    int done = -1;
    if (fscanf(f, "%d", &done) != 1) done = -1;
    fclose(f);
    return done;
}

static bool kampylos_write_checkpoint(int done) {
    return kampylos_write_file_atomic(KAMPYLOS_CHECKPOINT_NAME, std::to_string(done) + "\n");
}

static bool kampylos_read_seed_checkpoint(KampylosSeedCheckpoint &cp) {
    char path[512];
    if (boinc_resolve_filename(KAMPYLOS_SEED_CHECKPOINT_NAME, path, sizeof(path))) return false;
    FILE *f = boinc_fopen(path, "r");
    if (!f) return false;
    char buf[512] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    return kampylos_parse_seed_checkpoint(std::string(buf, n), &cp);
}

static bool kampylos_write_seed_checkpoint(const KampylosSeedCheckpoint &cp) {
    return kampylos_write_file_atomic(KAMPYLOS_SEED_CHECKPOINT_NAME, kampylos_format_seed_checkpoint(cp));
}

// The seed checkpoint only ever describes the cell it names; once that cell's line is in "out"
// it is stale, and removing it keeps a later cell from ever reading it by mistake.
static void kampylos_remove_seed_checkpoint() {
    char path[512];
    if (boinc_resolve_filename(KAMPYLOS_SEED_CHECKPOINT_NAME, path, sizeof(path))) return;
    boinc_delete_file(path);
}

// The GPU apps' in-cell checkpoint (the batched driver's state, see src/kampylos_batched_fit.h):
// a few hundred KB of text, written whenever BOINC asks for a checkpoint, same atomic write.
static const char *KAMPYLOS_BATCHED_CHECKPOINT_NAME = "kampylos_batched_checkpoint";

static bool kampylos_read_batched_checkpoint(std::string &text) {
    char path[512];
    if (boinc_resolve_filename(KAMPYLOS_BATCHED_CHECKPOINT_NAME, path, sizeof(path))) return false;
    FILE *f = boinc_fopen(path, "rb");
    if (!f) return false;
    text.clear();
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);
    return !text.empty();
}

static void kampylos_remove_batched_checkpoint() {
    char path[512];
    if (boinc_resolve_filename(KAMPYLOS_BATCHED_CHECKPOINT_NAME, path, sizeof(path))) return;
    boinc_delete_file(path);
}

struct KampylosBoincJob {
    KampylosWuInput in;
    std::vector<DataPoint> data;
    KampylosWuAnchor anchor;
    FILE *fout = nullptr;
    int start_cell = 0, total = 0;
};

// Resolves/parses "in" and loads "photometry". Returns 0 on success; on failure prints the
// reason and returns non-zero (the caller does boinc_finish(1)).
// allow_parts: only the CPU app can fit a part of each cell's seeds (input token "part i K", v12); the batched GPU
// backends refuse such a work unit instead of silently computing every seed.
static int kampylos_boinc_load_inputs(KampylosBoincJob &job, bool allow_parts = false) {
    char phot_path[512], in_path[512];
    if (boinc_resolve_filename("photometry", phot_path, sizeof(phot_path))) {
        fprintf(stderr, "can't resolve input file 'photometry'\n");
        return 1;
    }
    if (boinc_resolve_filename("in", in_path, sizeof(in_path))) {
        fprintf(stderr, "can't resolve input file 'in'\n");
        return 1;
    }
    FILE *fin = boinc_fopen(in_path, "r");
    if (!fin) {
        fprintf(stderr, "can't open input file %s\n", in_path);
        return 1;
    }
    std::string err;
    bool ok = kampylos_read_input_file(fin, &job.in, &err);
    fclose(fin);
    if (!ok) {
        fprintf(stderr, "malformed input file: %s\n", err.c_str());
        return 1;
    }
    if (job.in.nparts > 1 && !allow_parts) {
        fprintf(stderr, "this backend cannot run a seed-split work unit (part %d of %d)\n", job.in.part, job.in.nparts);
        return 1;
    }
    try {
        job.data = load_photometry(phot_path, job.in.is_mag);
    } catch (const std::exception &e) {
        fprintf(stderr, "error loading photometry: %s\n", e.what());
        return 1;
    }
    if (job.data.size() < 20) {
        fprintf(stderr, "error: only %zu usable data points (need at least 20)\n", job.data.size());
        return 1;
    }
    job.total = job.in.n_s * job.in.n_q;
    const char *g = kampylos_grid_name(job.in.log10_grid);
    fprintf(stderr, "Loaded %zu data points; grid %dx%d (%s), %s_s [%.4f,%.4f], %s_q [%.4f,%.4f]\n",
            job.data.size(), job.in.n_s, job.in.n_q, g, g, job.in.s_min, job.in.s_max, g, job.in.q_min, job.in.q_max);
    return 0;
}

static void kampylos_boinc_compute_anchor(KampylosBoincJob &job) {
    job.anchor = kampylos_compute_anchor(job.data);
    kampylos_log_anchor(stderr, job.anchor);
}

// Opens "out": fresh (header written) on a first run; on a resume, continues after its complete
// cell lines (a torn last line is cut off first). Returns 0 on success.
static int kampylos_boinc_open_output(KampylosBoincJob &job, const char *backend) {
    char out_path[512];
    if (boinc_resolve_filename("out", out_path, sizeof(out_path))) {
        fprintf(stderr, "can't resolve output file 'out'\n");
        return 1;
    }
    int checkpoint = kampylos_read_checkpoint();                 // -1: none
    int have = kampylos_count_complete_cells(out_path);           // -1: no usable output yet
    job.start_cell = 0;
    if (have >= 0) {
        job.start_cell = std::min(have, job.total);
        if (checkpoint >= 0 && checkpoint != have)
            fprintf(stderr, "checkpoint says %d cells done but the output file holds %d complete cells -- resuming from %d\n",
                    checkpoint, have, job.start_cell);
        else if (checkpoint < 0 && have > 0)
            fprintf(stderr, "no checkpoint file; the output file holds %d complete cells -- resuming from there\n", have);
    } else if (checkpoint > 0) {
        fprintf(stderr, "checkpoint says %d cells done but there is no usable output file -- restarting from cell 0\n", checkpoint);
    }
    if (job.start_cell > 0 && !kampylos_trim_output(out_path, job.start_cell)) {
        fprintf(stderr, "could not trim the output file to %d cells -- restarting from cell 0\n", job.start_cell);
        job.start_cell = 0;
    }
    fprintf(stderr, "Resuming from cell %d/%d\n", job.start_cell, job.total);
    job.fout = boinc_fopen(out_path, job.start_cell == 0 ? "w" : "a");
    if (!job.fout) {
        fprintf(stderr, "can't open %s for writing\n", out_path);
        return 1;
    }
    if (job.start_cell == 0) {
        kampylos_write_header(job.fout, job.in, job.anchor, backend);
        fflush(job.fout);
    }
    return 0;
}

// Called after each cell's line has been written and flushed: progress, then the cell
// checkpoint -- always, not only when boinc_time_to_checkpoint() says so (see the top comment).
// boinc_checkpoint_completed() ends the critical section and tells the client where the task
// can be restarted from.
static void kampylos_boinc_after_cell(int done, int total) {
    boinc_fraction_done((double)done / (double)total);
    boinc_begin_critical_section();
    if (!kampylos_write_checkpoint(done))
        fprintf(stderr, "warning: could not write the cell checkpoint after cell %d\n", done);
    kampylos_remove_seed_checkpoint();
    kampylos_remove_batched_checkpoint();
    boinc_checkpoint_completed();
}

// Runs every remaining cell with `fit_cell` and finishes the WU.
template <class FitCell>
static int kampylos_boinc_run(KampylosBoincJob &job, VBMicrolensing &vbm, FitCell fit_cell) {
    kampylos_run_cells(job.fout, stderr, job.in, job.data, job.anchor, vbm, job.start_cell, job.total,
                       fit_cell, kampylos_boinc_after_cell);
    fclose(job.fout);
    boinc_fraction_done(1.0);
    boinc_finish(0);
    return 0;
}

// The CPU app's seed-level checkpoint, mapped onto the BOINC API (see src/kampylos_cpu_cell.h).
// boinc_time_to_checkpoint() opens a critical section when it returns true and
// boinc_checkpoint_completed() closes it, so the client cannot stop the task between the two.
inline KampylosCellHooks kampylos_boinc_cell_hooks(int cell, int total) {
    KampylosCellHooks h;
    h.read_checkpoint = [](KampylosSeedCheckpoint &cp) { return kampylos_read_seed_checkpoint(cp); };
    h.write_checkpoint = [](const KampylosSeedCheckpoint &cp) {
        if (!kampylos_write_seed_checkpoint(cp))
            fprintf(stderr, "warning: could not write the seed checkpoint (cell %d, seed %lu)\n", cp.cell, (unsigned long)cp.next);
    };
    h.time_to_checkpoint = []() { return boinc_time_to_checkpoint() != 0; };
    h.checkpoint_completed = []() { boinc_checkpoint_completed(); };
    h.progress = [cell, total](double own) { boinc_fraction_done((cell + own) / (double)total); };
    h.keep_going = nullptr;
    return h;
}

// The GPU apps' in-cell checkpoint and progress, mapped onto the BOINC API the same way (see
// src/kampylos_batched_fit.h). The wrappers pass this and the cell index to their backend's
// *_fit_binary_seeds().
inline KampylosBatchedHooks kampylos_boinc_batched_hooks(int cell, int total) {
    KampylosBatchedHooks h;
    h.read_checkpoint = [](std::string &text) { return kampylos_read_batched_checkpoint(text); };
    h.write_checkpoint = [](const std::string &text) {
        if (!kampylos_write_file_atomic(KAMPYLOS_BATCHED_CHECKPOINT_NAME, text))
            fprintf(stderr, "warning: could not write the in-cell checkpoint\n");
    };
    h.time_to_checkpoint = []() { return boinc_time_to_checkpoint() != 0; };
    h.checkpoint_completed = []() { boinc_checkpoint_completed(); };
    h.progress = [cell, total](double own) { boinc_fraction_done((cell + own) / (double)total); };
    h.keep_going = nullptr;
    return h;
}

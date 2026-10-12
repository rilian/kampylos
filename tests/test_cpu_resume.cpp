// Checks the CPU app's resumable seed loop (src/kampylos_cpu_cell.h) and the result-file
// resume helpers (kampylos_split_output / kampylos_count_complete_cells / kampylos_trim_output):
//   1. the seed checkpoint line round-trips every double exactly;
//   2. a cell fitted in two runs -- stopped after seed k as a kill would, checkpoint written,
//      resumed from it -- returns bit-for-bit the result of one uninterrupted run, also for a
//      seed-split ("part i K") work unit;
//   3. a result file with a torn last line is counted/trimmed to its complete lines only.
// Uses the committed synthetic planet light curve and a handful of its seeds, so it runs in
// well under a minute.
//
// Build (from the repo root):
//   g++ -O2 -std=c++17 -I src -I vendor/VBMicrolensing/VBMicrolensing/lib tests/test_cpu_resume.cpp \
//       vendor/VBMicrolensing/VBMicrolensing/lib/VBMicrolensingLibrary.cpp -o test_cpu_resume
//   ./test_cpu_resume tests/regression/synthetic_planet.dat
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include "kampylos_wu.h"
#include "kampylos_cpu_cell.h"

static int fails = 0;
static void check(bool ok, const char* what) {
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

static bool same_result(const BinaryFitResult& a, const BinaryFitResult& b) {
    return memcmp(&a, &b, sizeof(BinaryFitResult)) == 0;
}

// In-memory stand-in for the BOINC checkpoint plumbing.
struct MemoryCheckpoint {
    bool present = false;
    std::string text;
    size_t stop_after_seed = (size_t)-1;   // keep_going() returns false once this many seeds ran
    size_t seeds_run = 0;
    KampylosCellHooks hooks() {
        KampylosCellHooks h;
        h.read_checkpoint = [this](KampylosSeedCheckpoint& cp) { return present && kampylos_parse_seed_checkpoint(text, &cp); };
        h.write_checkpoint = [this](const KampylosSeedCheckpoint& cp) { text = kampylos_format_seed_checkpoint(cp); present = true; };
        h.time_to_checkpoint = []() { return true; };   // checkpoint after every seed
        h.checkpoint_completed = []() {};
        h.progress = [](double) {};
        h.keep_going = [this]() { return ++seeds_run < stop_after_seed; };
        return h;
    }
};

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <photometry (flux)>\n", argv[0]); return 2; }
    std::vector<DataPoint> data = load_photometry(argv[1], false);
    printf("loaded %zu points\n", data.size());

    printf("== seed checkpoint round trip\n");
    {
        KampylosSeedCheckpoint c;
        c.cell = 7; c.next = 123;
        c.best = { 0.46051701859880917, -6.9077552789821368, 2455999.9951018, -0.10007499999999999, 19.997749, 6.1475480000000004,
                   1.155637e-06, 1.0014299999999999, 0.29869000000000002, 411.22280000000001 };
        std::string line = kampylos_format_seed_checkpoint(c);
        KampylosSeedCheckpoint d;
        check(kampylos_parse_seed_checkpoint(line, &d), "checkpoint line parses");
        check(d.cell == 7 && d.next == 123 && same_result(c.best, d.best), "every field round-trips exactly");
        check(!kampylos_parse_seed_checkpoint("7 123 1 2 3", &d), "a torn line is rejected");
    }

    printf("== resumable cell\n");
    KampylosWuAnchor anchor = kampylos_compute_anchor(data);
    const double ln_s = log(pow(10.0, 0.2)), ln_q = log(1e-3);
    BinarySeedSet all = kampylos_make_seeds(anchor.seeds, exp(ln_s), exp(ln_q));
    BinarySeedSet seeds;
    for (size_t k = 0; k < all.size(); k += all.size() / 6) seeds.add(all.x0[k], all.step[k]);   // 6-7 seeds
    printf("   %zu of %zu seeds\n", seeds.size(), all.size());
    VBMicrolensing vbm;

    MemoryCheckpoint none;
    BinaryFitResult full = kampylos_fit_cell_resumable(vbm, data, ln_s, ln_q, seeds, 3, 0, 1, none.hooks(), stdout);
    printf("   uninterrupted: chi2=%.6f\n", full.chi2);

    MemoryCheckpoint killed;
    killed.stop_after_seed = 2;
    bool completed = true;
    BinaryFitResult partial = kampylos_fit_cell_resumable(vbm, data, ln_s, ln_q, seeds, 3, 0, 1, killed.hooks(), stdout, &completed);
    check(!completed && killed.present, "stopped after 2 seeds with a checkpoint written");
    KampylosSeedCheckpoint written;
    check(kampylos_parse_seed_checkpoint(killed.text, &written) && written.cell == 3 && written.next == 2 && same_result(written.best, partial),
          "checkpoint holds cell 3, next seed 2 and the best fit so far");
    MemoryCheckpoint resumed; resumed.present = true; resumed.text = killed.text;
    BinaryFitResult second = kampylos_fit_cell_resumable(vbm, data, ln_s, ln_q, seeds, 3, 0, 1, resumed.hooks(), stdout, &completed);
    check(completed, "resumed run completes");
    check(same_result(full, second), "resumed result is bit-identical to the uninterrupted one");

    MemoryCheckpoint wrong_cell; wrong_cell.present = true; wrong_cell.text = killed.text;
    BinaryFitResult other = kampylos_fit_cell_resumable(vbm, data, ln_s, ln_q, seeds, 4, 0, 1, wrong_cell.hooks(), stdout, &completed);
    check(same_result(full, other), "a checkpoint for another cell is ignored (cell redone from seed 0)");

    // seed-split: part 1 of 2 must only ever use the odd seeds, with or without a kill
    MemoryCheckpoint p_none;
    BinaryFitResult part_full = kampylos_fit_cell_resumable(vbm, data, ln_s, ln_q, seeds, 3, 1, 2, p_none.hooks(), stdout);
    BinarySeedSet odd;
    for (size_t k = 1; k < seeds.size(); k += 2) odd.add(seeds.x0[k], seeds.step[k]);
    BinaryFitResult odd_ref = fit_binary_seeds(vbm, data, ln_s, ln_q, odd);
    check(same_result(part_full, odd_ref), "part 1 of 2 equals fit_binary_seeds over the odd seeds");
    MemoryCheckpoint p_killed; p_killed.stop_after_seed = 1;
    kampylos_fit_cell_resumable(vbm, data, ln_s, ln_q, seeds, 3, 1, 2, p_killed.hooks(), stdout, &completed);
    MemoryCheckpoint p_resumed; p_resumed.present = true; p_resumed.text = p_killed.text;
    BinaryFitResult part_second = kampylos_fit_cell_resumable(vbm, data, ln_s, ln_q, seeds, 3, 1, 2, p_resumed.hooks(), stdout, &completed);
    check(completed && same_result(part_full, part_second), "seed-split cell resumes bit-identically");

    printf("== result-file resume helpers\n");
    {
        std::string path = std::string(argv[0]) + ".out.tmp";
        FILE* f = fopen(path.c_str(), "wb");
        fprintf(f, "# grid=log10 log10_s log10_q chi2\n# anchor format=2 backend=cpu\n0.2 -5 1.0\n0.2 -4 2.0\n0.2 -3 3.");  // torn last line
        fclose(f);
        check(kampylos_count_complete_cells(path.c_str()) == 2, "torn last line is not counted");
        check(!kampylos_trim_output(path.c_str(), 3), "cannot trim to more cells than there are");
        check(kampylos_trim_output(path.c_str(), 2), "trim to the 2 complete cells");
        std::string whole;
        kampylos_split_output(path.c_str(), -1, nullptr, &whole);
        check(whole == "# grid=log10 log10_s log10_q chi2\n# anchor format=2 backend=cpu\n0.2 -5 1.0\n0.2 -4 2.0\n", "only the torn line was cut");
        check(kampylos_trim_output(path.c_str(), 2), "trimming an intact file to its own length succeeds");
        std::string again;
        kampylos_split_output(path.c_str(), -1, nullptr, &again);
        check(again == whole, "and leaves it unchanged");
        check(kampylos_trim_output(path.c_str(), 1), "trim to 1 cell");
        check(kampylos_count_complete_cells(path.c_str()) == 1, "1 complete cell after the trim");
        std::string kept;
        kampylos_split_output(path.c_str(), -1, &kept);
        check(kept == "# grid=log10 log10_s log10_q chi2\n# anchor format=2 backend=cpu\n0.2 -5 1.0\n", "header lines survive the trim");
        f = fopen(path.c_str(), "wb"); fprintf(f, "# log_s log_q chi2\n1 2 3\n"); fclose(f);
        check(kampylos_count_complete_cells(path.c_str()) == -1, "pre-format-2 file is not a usable output");
        check(kampylos_count_complete_cells("/nonexistent/kampylos.out") == -1, "missing file is not a usable output");
        remove(path.c_str());
    }

    printf("\n%s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}

#!/bin/sh
# End-to-end checkpoint/resume test of the real CPU BOINC wrapper (boinc/main_kampylos_boinc.cpp),
# built against the fake BOINC runtime in tests/boinc_stub so it runs as a plain process:
#   1. one uninterrupted run of a 2-cell work unit -> reference "out";
#   2. the same work unit killed with SIGKILL at random moments and restarted until it finishes,
#      with the cell checkpoint file deleted once in between (the output file alone must carry
#      the resume) -> must produce a byte-identical "out" (or one that differs at rounding
#      level only, see same_output below);
#   3. a run that starts from a stale checkpoint (more cells than the output holds) must resume
#      from the output's own line count, not restart;
#   4. a run whose checkpoint is BEHIND the output (killed between a cell's line and its
#      checkpoint write) must not redo that cell;
#   5. an output with a torn last line resumes after its complete lines.
# The light curve is the committed synthetic planet subsampled to ~60 points so a seed takes a
# fraction of a second; the stub checkpoints after every seed. Takes a few minutes.
#
# Usage (from the repo root): sh tests/test_boinc_resume.sh [work_dir]
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${1:-$ROOT/tests/work_boinc_resume}
VBM=$ROOT/vendor/VBMicrolensing/VBMicrolensing/lib
CXX=${CXX:-c++}
mkdir -p "$WORK"
cd "$WORK"

# KAMPYLOS_RESUME_BACKEND=metal runs the same test against the Metal wrapper (macOS only), which
# exercises the batched driver's in-cell checkpoint instead of the CPU app's seed checkpoint.
BACKEND=${KAMPYLOS_RESUME_BACKEND:-cpu}
echo "== building the $BACKEND wrapper against tests/boinc_stub"
if [ "$BACKEND" = metal ]; then
    $CXX -O2 -std=c++17 -fobjc-arc -I "$ROOT/src" -I "$VBM" -I "$ROOT/tests/boinc_stub" \
        "$ROOT/boinc/main_kampylos_metal_boinc.cpp" "$ROOT/src/kampylos_metal_fit.mm" \
        "$ROOT/tests/boinc_stub/boinc_stub.cpp" "$VBM/VBMicrolensingLibrary.cpp" \
        -framework Metal -framework Foundation -o kampylos_stub 2>&1 | grep -v VBMicrolensingLibrary || true
else
    $CXX -O2 -std=c++17 -I "$ROOT/src" -I "$VBM" -I "$ROOT/tests/boinc_stub" \
        "$ROOT/boinc/main_kampylos_boinc.cpp" "$ROOT/tests/boinc_stub/boinc_stub.cpp" "$VBM/VBMicrolensingLibrary.cpp" \
        -o kampylos_stub 2>&1 | grep -v VBMicrolensingLibrary || true
fi
test -x kampylos_stub

awk 'NR % 7 == 1' "$ROOT/tests/regression/synthetic_planet.dat" > photometry
echo "flux 0.2 0.2 1 -3.2 -3 2 log10" > in
export KAMPYLOS_STUB_CHECKPOINT_SECONDS=0

fresh() { rm -f out kampylos_checkpoint kampylos_seed_checkpoint kampylos_batched_checkpoint stub_checkpoints stub_fraction_done; }

# same_output A B: byte-identical, or -- VBMicrolensing's root solver (BinaryMag0 -> cmplx_roots_gen
# with use_roots_as_starting_points) starts from the previous call's roots, so a resumed process,
# whose VBMicrolensing instances have a different call history, can see a magnification differ in
# its last bits and Nelder-Mead can then take a slightly different path -- line by line: chi2 and
# the diagnostics within 1e-3, every other field within 1% (the same chi2 valley), integers equal.
# Returns 0 and prints which of the two it was; 1 if the files differ beyond that.
same_output() {
    if cmp -s "$1" "$2"; then echo "   ok    resumed output is byte-identical to the reference"; return 0; fi
    awk 'FNR == NR { a[FNR] = $0; n = FNR; next }
         FNR > n { bad = 1; exit }
         { split(a[FNR], x, " "); nf = split($0, y, " ");
           if (nf != length(x)) { bad = 1; exit }
           if ($0 ~ /^#/) { if ($0 != a[FNR]) { bad = 1; exit } next }
           for (i = 1; i <= nf; i++) {
               if (i == 3 || (i >= 11 && i <= 13)) { if ((x[i] - y[i]) > 1e-3 || (y[i] - x[i]) > 1e-3) { bad = 1; exit } }
               else if (i >= 14) { if (x[i] != y[i]) { bad = 1; exit } }
               else { d = x[i] - y[i]; if (d < 0) d = -d; m = x[i]; if (m < 0) m = -m; if (d > 0.01 * m + 1e-12) { bad = 1; exit } }
           } }
         END { exit bad }' "$1" "$2" \
    && { echo "   ok    resumed output matches the reference to rounding level (not byte-identical: see same_output)"; diff "$1" "$2" | head -4; return 0; }
    return 1
}

echo "== reference run (uninterrupted)"
fresh
./kampylos_stub > ref.log 2>&1
cp out ref.out
echo "   $(grep -c -v '^#' ref.out) cell lines, $(wc -l < stub_checkpoints) checkpoints"

echo "== killed and restarted run"
fresh
attempt=0
deleted_checkpoint=0
while :; do
    attempt=$((attempt + 1))
    ./kampylos_stub > "kill$attempt.log" 2>&1 &
    pid=$!
    # kill after 5-25 s (the interval grows so a slow seed cannot starve the run), unless it finishes first
    delay=$(( 5 + (attempt * 3) % 21 ))
    i=0
    while [ $i -lt $delay ] && kill -0 $pid 2>/dev/null; do sleep 1; i=$((i + 1)); done
    if kill -0 $pid 2>/dev/null; then
        kill -9 $pid; wait $pid 2>/dev/null || true
        lines=$(awk '!/^#/ { n++ } END { print n + 0 }' out 2>/dev/null || echo 0)
        if [ -f kampylos_seed_checkpoint ]; then cp_info="seed checkpoint: $(cut -c1-12 kampylos_seed_checkpoint)"
        elif [ -f kampylos_batched_checkpoint ]; then cp_info="in-cell checkpoint (cell seeds restarts pass round): $(sed -n 2p kampylos_batched_checkpoint)"
        else cp_info="no in-cell checkpoint"; fi
        echo "   attempt $attempt: killed after ${delay}s ($lines cell lines, $cp_info)"
        if [ $deleted_checkpoint -eq 0 ] && [ -f kampylos_checkpoint ] && [ "$(cat kampylos_checkpoint)" -ge 1 ]; then
            rm kampylos_checkpoint; deleted_checkpoint=1
            echo "   (deleted the cell checkpoint file to force a resume from the output file)"
        fi
    else
        wait $pid && echo "   attempt $attempt: finished" || { echo "   attempt $attempt: exited with error"; exit 1; }
        break
    fi
    [ $attempt -lt 60 ] || { echo "FAIL: did not finish in 60 attempts"; exit 1; }
done
grep -q "resuming cell" kill*.log || echo "   (note: no seed-level resume happened; kills landed between cells)"
same_output out ref.out || { echo "   FAIL  output differs"; diff out ref.out | head; exit 1; }

echo "== stale checkpoint (claims more cells than the output holds)"
fresh
head -n 3 ref.out > out        # header + first cell only
echo 2 > kampylos_checkpoint   # claims both
./kampylos_stub > stale.log 2>&1
grep -q "resuming from 1" stale.log && echo "   ok    resumed from the output's 1 complete cell" || { echo "   FAIL  did not resume from the output"; cat stale.log; exit 1; }
same_output out ref.out || { echo "   FAIL  output differs"; diff out ref.out | head; exit 1; }

echo "== checkpoint behind the output (killed between a cell's line and its checkpoint write)"
fresh
cp ref.out out                 # both cells
echo 1 > kampylos_checkpoint   # claims one
./kampylos_stub > behind.log 2>&1
grep -q "resuming from 2" behind.log && echo "   ok    resumed from the output's 2 complete cells" || { echo "   FAIL  did not resume from the output"; cat behind.log; exit 1; }
if grep -q '^\[' behind.log; then echo "   FAIL  a finished cell was recomputed"; exit 1; else echo "   ok    no cell recomputed"; fi
if cmp -s out ref.out; then echo "   ok    output byte-identical to the reference"; else echo "   FAIL  output differs"; exit 1; fi

echo "== torn last line (killed while writing cell 2's line)"
fresh
head -n 3 ref.out > out        # header + first cell
printf '0.2 -3 12' >> out      # the start of the second cell's line, no newline
echo 2 > kampylos_checkpoint
./kampylos_stub > torn.log 2>&1
grep -q "resuming from 1" torn.log && echo "   ok    resumed from the 1 complete cell" || { echo "   FAIL  did not resume from the complete lines"; cat torn.log; exit 1; }
same_output out ref.out || { echo "   FAIL  output differs"; diff out ref.out | head; exit 1; }

echo "PASS"

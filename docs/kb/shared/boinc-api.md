# BOINC API behaviour the applications depend on

Facts about the BOINC application library and client that the checkpoint, lifecycle and device code of every
bitboinc application relies on. Each one was read in the BOINC source, github.com/BOINC/boinc at commit `4d1529d`
(2026-10-25); the line numbers refer to that commit. When a fact here turns out to be wrong for some client or library
version, correct it here, date the correction, and sync.

## Checkpoints and critical sections

* `boinc_time_to_checkpoint()` returns 1 while the checkpoint interval has passed **and enters a critical section
  each time it returns 1** (`api/boinc_api.cpp:1646-1652`). The "time to checkpoint" flag stays set until
  `boinc_checkpoint_completed()`, so a second call in the same opportunity returns 1 again and enters a second time.
* `boinc_checkpoint_completed()` leaves the critical section once and clears the flag (`api/boinc_api.cpp:1654-1664`).
  So: call `boinc_time_to_checkpoint()` once per opportunity, keep the result in a variable, and pair every 1 with
  exactly one `boinc_checkpoint_completed()` (or `boinc_end_critical_section()` when the write failed and must not be
  reported). Pattern PAT-001.
* The critical section is a counter (`in_critical_section`, `api/boinc_api.cpp:164`, `1666-1700`).
  `boinc_end_critical_section()` clamps it at 0 (`1685-1688`), so an extra end, or a `boinc_checkpoint_completed()`
  without a preceding 1, does not make it negative. While it is above zero, suspend (`1302-1309`), quit
  (`1325-1336`), abort (`1337-1349`) and "client dead" (`1426-1438`) are only recorded; they are carried out when the
  counter returns to 0. A counter that never returns to 0 means the task ignores quit and abort and keeps computing
  while the client shows it suspended.
* Writing a checkpoint outside a critical section is legal but can be cut by a quit at any instruction (pattern
  PAT-004).

## Exit paths

* On a client quit, abort or "client dead", the library ends the process through `boinc_exit()`, which calls
  `fflush(NULL)` before exiting (`api/boinc_api.cpp:907-961`, flush at `943`). On Windows the timer thread calls it
  directly; on Unix the timer thread sets a flag and the worker thread's SIGALRM handler calls it at its next tick
  (`exit_from_timer_thread`, `996-1020`; handler `1528-1533`), so it can happen between any two statements, also in
  the middle of an `fprintf`. **Consequence:** output buffered in stdio after the last checkpoint reaches the disk at a
  quit, and a resume that appends to that file without cutting it back writes those rows twice. A test that kills with
  SIGKILL never flushes and cannot show this. Pattern PAT-002.
* `boinc_finish(status)` sets `finishing`, sleeps 2 s, writes `boinc_finish_called`, then calls `boinc_exit()`
  (`api/boinc_api.cpp:851-878`). Once `finishing` is set, the timer handler only sends a last status message and
  stops (`1379-1386`): **a trickle-up message sent just before `boinc_finish()` is not announced to the client during
  that sleep** (pattern PAT-009). A non-zero status is reported as a computation error.
* `boinc_temporary_exit(delay, reason, is_notice)` writes `temporary_exit` with the delay and exits with status 0 at
  once, without any wait (`api/boinc_api.cpp:884-899`). The client keeps the task and its slot directory
  (checkpoints included) and starts it again after `delay` seconds. It is the right exit for conditions that may go
  away (GPU busy or reset, device missing, waiting for preferences).
* The client counts premature exits of a task: every temporary exit, and every exit without `boinc_finish()`. After
  more than 100 it aborts the task as an error, "too many boinc_temporary_exit()s" or "too many exit(0)s"
  (`ERR_TOO_MANY_EXITS`, `client/app_control.cpp:362-372`, `383-392`). The count goes back to 0 only when the client
  sees a new checkpoint in a status message from the running task (`1559-1566`) or when the client itself restarts
  (`client/app.cpp:136`, `144`). A task that takes a temporary exit again and again, without the client seeing a
  checkpoint in between, is therefore aborted after 100 exits: at 300 s that is 8 hours, at 1800 s about 2 days
  (patterns PAT-013, PAT-030).

## Files

* `boinc_fopen()` (`lib/filesys.cpp:581-624`): for a mode containing `r` it returns NULL at once when the file does
  not exist (no retry); otherwise, on Windows, it retries a failed open for up to 5 s with `_fsopen(..., _SH_DENYNO)`
  (files held by antivirus or indexers), and on Unix it retries a few times after EINTR. A NULL from
  `boinc_fopen(path, "r")` therefore means "missing" *or* "could not be opened", and `errno` is not a reliable way to
  tell them apart (on Windows the retries overwrite it); use `boinc_file_exists()` (pattern PAT-005).
* `boinc_rename()` (`lib/filesys.cpp:752-795`) uses `MoveFileExA(..., MOVEFILE_REPLACE_EXISTING |
  MOVEFILE_WRITE_THROUGH)` on Windows and retries for a while if the target is busy. Plain `rename()` on Windows fails
  when the target exists, so "write temp, rename over the old file" needs `boinc_rename()` (pattern PAT-003).
* Text mode (`"w"`, `"a"`) writes CRLF line ends on Windows; binary mode (`"wb"`, `"ab"`) writes the same bytes on
  every platform (pattern PAT-019).

## Timer, signals and threads

* The library's timer runs at 10 Hz (`TIMER_PERIOD 0.1`, `api/boinc_api.cpp:207`); its slower actions (status
  message, trickle-up announcement) run once per second (`1409`, `1449-1451`).
* On Unix the worker thread gets a SIGALRM about every 0.1 s (`setitimer`, `api/boinc_api.cpp:1601-1628`; on Android
  the timer thread sends it with `pthread_kill`, `1020`, `1357`, `1521`). The handler is installed with `SA_RESTART`
  (`1610`): `read()` and `write()` that have not transferred anything yet are restarted, but `sleep`, `usleep`,
  `nanosleep`, `poll` and `select` return early (EINTR or the time left) whatever the flag. Loop with the remaining
  time, or measure elapsed time with a clock instead of counting sleeps (pattern PAT-010).
* Suspend stops only the worker thread unless the application started with `boinc_init_parallel()` (option
  `multi_thread`): on Windows the timer thread calls `SuspendThread()` on the worker thread alone; on Unix the worker
  sits in `sleep(1)` inside its signal handler while suspended (`api/boinc_api.cpp:1146-1180`, `1535-1540`). Threads
  the application created itself keep running. With `boinc_init_parallel()` the library forks and suspends the whole
  process (Unix) or all its threads (Windows) (`622-662`, `1157`, `1191`). Pattern PAT-025.
* While the task is suspended, wall-clock time passes while the code does not run.

## Trickle messages

* `boinc_send_trickle_up()` writes the fixed file `trickle_up.xml` in the slot directory with mode `"wb"` and sets a
  flag (`api/boinc_api.cpp:1632-1644`). On a one-second tick the timer thread announces it to the client
  (`send_trickle_up_msg`, `828-845`), and the client renames the file into the project directory when it processes
  that message on its next poll (`client/app.cpp:672-693`, `client/app_control.cpp:1527-1580`). A second send
  before that overwrites the first message. An exit within about two seconds of a send can lose it, also through
  `boinc_finish()` (see Exit paths) or `boinc_temporary_exit()`. Pattern PAT-009.

## GPU assignment (init_data.xml)

* `APP_INIT_DATA` carries `gpu_type`, `gpu_device_num` and `gpu_opencl_dev_index` (`lib/app_ipc.h:189-191`). The
  client also appends `--device <gpu_device_num>` to the command line of GPU apps (`client/app_start.cpp:107-126`);
  for OpenCL it can differ from `gpu_opencl_dev_index`. The client chooses the device; an application that uses its own
  default, or a `--device` written into the work unit's command line, runs on the wrong GPU on multi-GPU hosts
  (pattern PAT-011).
* `gpu_opencl_dev_index` counts the `CL_DEVICE_TYPE_GPU` devices of the platform whose vendor matches `gpu_type`.
  The library's own helper `boinc_get_opencl_ids()` (`api/boinc_opencl.cpp:80-110`, `168-300`) does exactly that
  lookup; an application that enumerates devices itself must use the same platform and the same device type
  (pattern PAT-012).

LOA HYBRID: JOINT VNF PARALLELIZATION AND PLACEMENT
====================================================

This project places a request's VNFs (virtual network functions) on the
nodes of a mobile edge network so that its data finishes as early as
possible. It combines two papers and adds an exact search:

  Paper 1  Xie et al., "Latency-Sensitive and Resource-Efficient Parallel
           VNF Placement in Mobile Edge Networks: A Dynamic Graph
           Weighting Approach", IEEE/ACM ToN, 2026          -> LOA, ROA
  Paper 2  Tian, Liang and Liu, "Joint VNF Parallelization and Deployment
           in Mobile Edge Networks", IEEE TWC, 2023          -> SCA, VDA

The hybrid runs paper 2 (SCA + VDA) for a first answer, searches every
series/parallel layout x every placement exactly (branch and bound), and
uses paper 1's LOA / ROA only if that search runs out of time. It then
synchronises parallel branches so packets reach each merge node together.

All logic is in C++. The web pages only display what the C++ engine returns.

More detail:  FLOW.md      how the code works, results, differences from the papers
              HANDOVER.md  change history, known issues, review checklist


1. REQUIRED SOFTWARE
--------------------

Required:
  - A C++14 compiler. Tested with g++ 16.1.0 from MSYS2 UCRT64 on Windows 11.
      Download MSYS2 from https://www.msys2.org, then in the "MSYS2 UCRT64"
      terminal run:
          pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make
  - A web browser (Chrome, Edge or Firefox) to use the web pages.

Optional:
  - Python 3 (tested with 3.14), only for tests\count_sp.py. It uses only
    the standard library (itertools, fractions, math); no pip packages.
  - Git, to clone or update the project.
  - GMP (mingw-w64-ucrt-x86_64-gmp), only for "build.bat gmp", which counts
    very large search spaces exactly. Without it the counts saturate; the
    results are the same. The GMP build has not been tested.

Libraries:
  - Only the C++ standard library. On Windows the server also links
    ws2_32 (Windows sockets), which comes with the compiler. No other
    libraries, no package manager, no npm.
  - The web pages use no JavaScript libraries. They load fonts from Google
    Fonts when online; offline they fall back to system fonts.

Operating system:
  - Windows 10 / 11 is tested. Linux and macOS should build with "make"
    (the code has POSIX socket support) but this is untested.


2. CONFIGURE
------------

The compiler's bin folder must be on PATH, otherwise the build fails without
a clear message. On this machine it is D:\new_software\ucrt64\bin; with a
default MSYS2 install it is C:\msys64\ucrt64\bin.

  PowerShell:       $env:Path = "D:\new_software\ucrt64\bin;" + $env:Path
  Command Prompt:   set PATH=D:\new_software\ucrt64\bin;%PATH%

Check it:           g++ --version

To make this permanent, add the folder to PATH in Windows "Edit the system
environment variables".

Settings for one run are in the configuration JSON (the web form writes it;
examples in cpp\tests\preset_*.json). The main ones:
  nodes, links, src, dst, vnfs   the network and the request
  trep, tint, pkt                copy / merge time per unit, data size l_u
  tmax                           latency limit (ms)
  kmax, candCap                  LOA search limits (k and candidates per anchor)
  fullMs                         time budget of the full search (ms)
  full, seed, compare, roa       switch steps on or off
  sync, sMin, sMax, bufMax       packet sync: on/off, CPU speed range, buffer limit (kbit)

Experiment search limits are environment settings (see section 5):
  KMAX (default 1024), CAND (default 200)


3. COMPILE
----------

All commands below run from the cpp folder:

  cd "E:\4th yr\ACN\ACN_PROJECT\loa-hybrid\cpp"

Option A: build and start the web server in one step
  .\build.bat

Option B: build both programs with make
  mingw32-make            (Windows)
  make                    (Linux / macOS, untested)

Option C: by hand
  g++ -std=c++14 -O2 -Wall -Iinclude src\common\json.cpp src\common\model.cpp src\paper2\sca_vda.cpp src\paper1\loa.cpp src\hybrid\full.cpp src\hybrid\sync.cpp src\hybrid\bench.cpp src\app\summary.cpp src\hybrid\hybrid.cpp src\app\server.cpp src\app\main.cpp -o bin\loa_hybrid.exe -lws2_32 -static-libgcc -static-libstdc++

  g++ -std=c++14 -O2 -Wall -Iinclude src\common\json.cpp src\common\model.cpp src\paper2\sca_vda.cpp src\paper1\loa.cpp src\hybrid\full.cpp src\hybrid\sync.cpp src\app\experiments.cpp -o bin\loa_experiments.exe -static-libgcc -static-libstdc++

Create the bin folder first if it does not exist (mkdir bin).

Output:
  bin\loa_hybrid.exe       engine, web server, self-test, feasibility benchmark
  bin\loa_experiments.exe  paper 1's experiments and the same-request test


4. RUN
------

Web interface
  .\build.bat                        build, then serve http://localhost:8080
  or, if already built:
  .\bin\loa_hybrid.exe --serve --port 8080 --web ..\web

  Keep the window open; Ctrl+C stops the server. Open the pages through
  http://localhost:8080, not by double-clicking the HTML files.

  http://localhost:8080/                  calculator
  http://localhost:8080/results.html      paper 1's experiments, ours vs the paper
  http://localhost:8080/feasibility.html  how large n can be for the full search

  Calculator: pick a preset (or edit the network), then click
  "Run C++ engine" at the bottom of the right-hand column. The Results tab
  opens by itself; it stays greyed out until a run finishes.

  Presets:
    D   parallel wins                       58 ms
    E   serial wins (heavy copy/merge cost) 50 ms
    G   branch inside a level (paper 2)     68 ms
    R   reliability limit                   58 ms
    S   reuse of existing instances         55 ms
    L   larger network (5 M schemes)        67 ms, about 9 s
    P   packet sync: 5 ms and 15 ms branches, gap 10 -> 0 ms
    X1  LOA 49 ms vs hybrid 44 ms           (LOA at k <= 256)
    X2  LOA 55 ms vs hybrid 48 ms           (LOA at k <= 256)

One configuration from the command line (report JSON on stdout)
  .\bin\loa_hybrid.exe tests\preset_d.json > out.json
  type tests\preset_d.json | .\bin\loa_hybrid.exe -

Help
  .\bin\loa_hybrid.exe --help


5. EXPERIMENTS
--------------

Paper 1's Section V sweep (8 experiments in parallel; results go to
cpp\results\ and are copied to web\data\results\ for results.html):
  .\run_experiments.bat 10              10 runs per point, about 5-6 hours
  .\run_experiments.bat 5               about half the time
  .\run_experiments.bat 10 netsize      one experiment only
  Experiments: capacity bandwidth requests netsize sfclen parallel heavytail weights

  Keep the window open and the PC awake until it prints "Done". It prints
  nothing while it waits; progress is in cpp\results\<name>.log. A stopped
  run keeps the previous results. To change the search limits first:
    $env:KMAX = "4096"; $env:CAND = "500"      (PowerShell; much slower)

Same-request test (plain LOA and the hybrid on the same request):
  mkdir results     (if it does not exist)
  .\bin\loa_experiments.exe --paired --runs 3 --kmax 1024 --cand 200 --full-ms 2000 --only sfclen --out results\paired_sfclen.json
  Prints PAIRED lines (LOA vs hybrid) and SYNC lines (packet sync before -> after).
  Other options: --reqs N (requests per run), --sfc N (chain length), --staged

Feasibility benchmark (refreshes web\data\feasibility.json):
  .\bin\loa_hybrid.exe --feasibility --runs 5 --budget 10000 --out ..\web\data\feasibility.json


6. TEST
-------

Self-test (main correctness check, about 1 minute for 300):
  .\bin\loa_hybrid.exe --selftest 300
  Expected last line: SELFTEST OK
  It builds random small networks and checks that
    - the full search equals plain enumeration of every scheme, on both objectives;
    - LOA, ROA and VDA never beat it;
    - packet sync never raises latency, gap or buffer, keeps CPU speeds in
      range and does not exceed any node's capacity.
  A failing instance is saved as selftest_fail_<n>.json.

Structure counts (independent check, about a minute):
  python tests\count_sp.py
  Expected: 1, 3, 19, 195, 2791 layouts for a level of 1-5 VNFs.

Presets give the latencies listed in section 4, all labelled OPTIMAL.


7. TROUBLESHOOTING
------------------

"g++ is not recognized", or the build stops with no message
    The compiler is not on PATH: see section 2.
"cannot open output file bin\...exe: Permission denied"
    That program is still running. Close the server window, or run
    Stop-Process -Name loa_experiments, then build again.
"port 8080 in use"
    A server is already running: open the page, or use --port 8081.
Page says "C++ engine offline"
    The server is not running, or the page was opened as a file.
Page looks old after an update
    Press Ctrl+F5.
Results tab is greyed out
    Click "Run C++ engine" first.


8. FOLDER LAYOUT
----------------

  cpp\src\common\   model (network, layouts, delay model), json
  cpp\src\paper1\   loa (paper 1: LOA, ROA)
  cpp\src\paper2\   sca_vda (paper 2: SCA, VDA)
  cpp\src\hybrid\   hybrid (pipeline), full (exact search), sync (packet sync), bench (tests)
  cpp\src\app\      main, server, summary, experiments
  cpp\include\      headers, same folders
  cpp\tests\        preset_*.json, count_sp.py
  cpp\bin\          built programs (not in git)
  cpp\results\      raw experiment output (not in git)
  web\              index.html, results.html, feasibility.html, js\, css\, data\

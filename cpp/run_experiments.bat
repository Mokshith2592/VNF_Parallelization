@echo off
rem LOA Hybrid - build loa_experiments.exe, re-run paper 1's delay/resource experiments
rem and copy the results to web\data\results\ so results.html shows them.
rem   run_experiments.bat                 all 8 experiments, 10 runs per point as in the paper (many hours)
rem   run_experiments.bat 5               5 runs per point (a few hours)
rem   run_experiments.bat 10 netsize      only the listed experiments (any of: capacity bandwidth
rem                                       requests netsize sfclen parallel heavytail weights)
rem Search limits (paper 1 has none; the code needs them to finish). Change them before running:
rem   set KMAX=4096
rem   set CAND=500
rem KMAX 1024 / CAND 200 is about 10x slower than KMAX 256. Each log line shows how often a limit was hit.
rem Keep this window open until it says "Done": closing it stops the experiments.
rem Progress of each experiment is written to results\<name>.log.
cd /d "%~dp0"
if not exist bin mkdir bin
if not exist results mkdir results
set RUNS=%1
if "%RUNS%"=="" set RUNS=10
set EXPS=%2 %3 %4 %5 %6 %7 %8 %9
if "%2"=="" set EXPS=capacity bandwidth requests netsize sfclen parallel heavytail weights
if "%KMAX%"=="" set KMAX=1024
if "%CAND%"=="" set CAND=200
g++ -std=c++14 -O2 -Wall -Iinclude src\common\json.cpp src\common\model.cpp src\paper2\sca_vda.cpp src\paper1\loa.cpp src\hybrid\full.cpp src\hybrid\sync.cpp src\app\experiments.cpp -o bin\loa_experiments.exe -static-libgcc -static-libstdc++
if errorlevel 1 (
  echo Build failed. Is D:\new_software\ucrt64\bin on PATH?
  exit /b 1
)
for %%E in (%EXPS%) do (
  echo Starting %%E ^(%RUNS% runs per point^), log: results\%%E.log
  start "exp %%E" /b cmd /c "bin\loa_experiments.exe --runs %RUNS% --kmax %KMAX% --cand %CAND% --only %%E --out results\%%E.json > results\%%E.log 2>&1"
)
echo Waiting for the experiments to finish...

:wait
rem pause ~30 s (ping works even without a console, unlike timeout)
"%SystemRoot%\System32\ping.exe" -n 31 127.0.0.1 >nul
"%SystemRoot%\System32\tasklist.exe" /FI "IMAGENAME eq loa_experiments.exe" | "%SystemRoot%\System32\find.exe" /i "loa_experiments.exe" >nul
if not errorlevel 1 goto wait

if not exist ..\web\data\results mkdir ..\web\data\results
for %%E in (%EXPS%) do call :copyone %%E
echo Done. Start the server (build.bat) and open http://localhost:8080/results.html
exit /b 0

:copyone
rem copy only a complete (non-empty, freshly renamed) results file
if exist results\%1.json.tmp (
  echo %1 did not finish - see results\%1.log. The old web\data\results\%1.json is kept.
  exit /b 0
)
if not exist results\%1.json (
  echo %1 produced no results - see results\%1.log
  exit /b 0
)
for %%F in (results\%1.json) do if %%~zF==0 (
  echo %1 results file is empty - see results\%1.log
  exit /b 0
)
copy /Y results\%1.json ..\web\data\results\%1.json >nul
echo Copied %1.json to web\data\results\
exit /b 0

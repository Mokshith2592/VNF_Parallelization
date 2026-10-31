@echo off
rem LOA Hybrid - build and start the server (Windows, MinGW g++)
rem   build.bat          build + run on http://localhost:8080
rem   build.bat gmp      build with exact GMP counting (needs GMP dev files)
cd /d "%~dp0"
if not exist bin mkdir bin
set GMPFLAGS=
set GMPLIBS=
if /i "%1"=="gmp" (
  set GMPFLAGS=-DUSE_GMP
  set GMPLIBS=-lgmpxx -lgmp
)
g++ -std=c++14 -O2 -Wall -Iinclude %GMPFLAGS% src\common\json.cpp src\common\model.cpp src\paper2\sca_vda.cpp src\paper1\loa.cpp src\hybrid\full.cpp src\hybrid\sync.cpp src\hybrid\bench.cpp src\app\summary.cpp src\hybrid\hybrid.cpp src\app\server.cpp src\app\main.cpp -o bin\loa_hybrid.exe -lws2_32 -static-libgcc -static-libstdc++ %GMPLIBS%
if errorlevel 1 (
  echo Build failed.
  exit /b 1
)
echo Build OK: bin\loa_hybrid.exe
bin\loa_hybrid.exe --serve --port 8080 --web ..\web

/* =============================================================
   LOA Hybrid — Built-in HTTP server (Option 1: everything in C++)
     GET  /            → web/index.html (and every other static file)
     GET  /api/health  → {"ok":true,...}
     POST /api/run     → body = config JSON, reply = runHybrid() report
   Listens on 127.0.0.1 only. One request at a time.
   ============================================================= */
#pragma once
#include <string>

int serve(int port, const std::string& webRoot);

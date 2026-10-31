/* =============================================================
   LOA Hybrid — Preset Configurations
   Globals provided: PRESETS
   The same configs live in cpp/tests/preset_*.json for the command line.
   ============================================================= */

/* Each preset has:
   - label  : short display name
   - blurb  : one-line description shown in the card
   - cfg    : full configuration object POSTed to /api/run
*/
const PRESETS = [
  {
    "label": "Preset D — parallel wins",
    "blurb": "4 VNFs, light replication/integration overhead: f2 and f3 run in parallel.",
    "cfg": {
      "nodes": [
        {
          "name": "A",
          "cap": 40,
          "x": 90,
          "y": 170,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "B",
          "cap": 60,
          "x": 270,
          "y": 70,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "C",
          "cap": 50,
          "x": 300,
          "y": 170,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "D",
          "cap": 55,
          "x": 270,
          "y": 270,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "E",
          "cap": 40,
          "x": 510,
          "y": 170,
          "fault": 0,
          "inst": ""
        }
      ],
      "links": [
        {
          "a": 0,
          "b": 4,
          "delay": 18,
          "bw": 300
        },
        {
          "a": 0,
          "b": 1,
          "delay": 9,
          "bw": 300
        },
        {
          "a": 1,
          "b": 4,
          "delay": 12,
          "bw": 300
        },
        {
          "a": 0,
          "b": 2,
          "delay": 10,
          "bw": 300
        },
        {
          "a": 2,
          "b": 4,
          "delay": 14,
          "bw": 300
        },
        {
          "a": 0,
          "b": 3,
          "delay": 13,
          "bw": 300
        },
        {
          "a": 3,
          "b": 4,
          "delay": 15,
          "bw": 300
        },
        {
          "a": 1,
          "b": 2,
          "delay": 8,
          "bw": 300
        },
        {
          "a": 2,
          "b": 3,
          "delay": 9,
          "bw": 300
        }
      ],
      "src": 0,
      "dst": 4,
      "vnfs": [
        {
          "name": "f1",
          "level": 1,
          "proc": 9,
          "dem": 20,
          "fault": 0
        },
        {
          "name": "f2",
          "level": 2,
          "proc": 12,
          "dem": 25,
          "fault": 0
        },
        {
          "name": "f3",
          "level": 2,
          "proc": 15,
          "dem": 25,
          "fault": 0
        },
        {
          "name": "f4",
          "level": 3,
          "proc": 10,
          "dem": 30,
          "fault": 0
        }
      ],
      "trep": 2,
      "tint": 1,
      "tmax": 200,
      "kmax": 1024,
      "rmin": 0,
      "alpha": 0,
      "candCap": 30000,
      "seed": true,
      "order": true,
      "compare": true,
      "breq": 50,
      "qcpu": 20,
      "instCap": 100,
      "wBw": 0.5,
      "roa": true,
      "pkt": 1,
      "isfg": "all",
      "full": true,
      "fullMs": 20000,
      "sync": true,
      "sMin": 0.5,
      "sMax": 1.5,
      "bufMax": 0
    }
  },
  {
    "label": "Preset E — serial wins",
    "blurb": "Same network, heavy overhead: everything runs serially. The stability filter removes a path, so the exact check verifies.",
    "cfg": {
      "nodes": [
        {
          "name": "A",
          "cap": 45,
          "x": 90,
          "y": 170,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "B",
          "cap": 55,
          "x": 270,
          "y": 70,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "C",
          "cap": 45,
          "x": 300,
          "y": 170,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "D",
          "cap": 40,
          "x": 270,
          "y": 270,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "E",
          "cap": 45,
          "x": 510,
          "y": 170,
          "fault": 0,
          "inst": ""
        }
      ],
      "links": [
        {
          "a": 0,
          "b": 4,
          "delay": 18,
          "bw": 300
        },
        {
          "a": 0,
          "b": 1,
          "delay": 9,
          "bw": 300
        },
        {
          "a": 1,
          "b": 4,
          "delay": 12,
          "bw": 300
        },
        {
          "a": 0,
          "b": 2,
          "delay": 10,
          "bw": 300
        },
        {
          "a": 2,
          "b": 4,
          "delay": 14,
          "bw": 300
        },
        {
          "a": 0,
          "b": 3,
          "delay": 13,
          "bw": 300
        },
        {
          "a": 3,
          "b": 4,
          "delay": 15,
          "bw": 300
        },
        {
          "a": 1,
          "b": 2,
          "delay": 8,
          "bw": 300
        },
        {
          "a": 2,
          "b": 3,
          "delay": 9,
          "bw": 300
        }
      ],
      "src": 0,
      "dst": 4,
      "vnfs": [
        {
          "name": "f1",
          "level": 1,
          "proc": 8,
          "dem": 25,
          "fault": 0
        },
        {
          "name": "f2",
          "level": 2,
          "proc": 5,
          "dem": 25,
          "fault": 0
        },
        {
          "name": "f3",
          "level": 2,
          "proc": 8,
          "dem": 25,
          "fault": 0
        },
        {
          "name": "f4",
          "level": 3,
          "proc": 8,
          "dem": 25,
          "fault": 0
        }
      ],
      "trep": 6,
      "tint": 5,
      "tmax": 200,
      "kmax": 1024,
      "rmin": 0,
      "alpha": 0,
      "candCap": 30000,
      "seed": true,
      "order": true,
      "compare": true,
      "breq": 50,
      "qcpu": 20,
      "instCap": 100,
      "wBw": 0.5,
      "roa": true,
      "pkt": 1,
      "isfg": "all",
      "full": true,
      "fullMs": 20000,
      "sync": true,
      "sMin": 0.5,
      "sMax": 1.5,
      "bufMax": 0
    }
  },
  {
    "label": "Preset G — branch inside a level wins",
    "blurb": "Paper 2, Fig. 1(g): (f3 → f2) ∥ f4 needs one copy and one merge fewer than f2 ∥ f3 ∥ f4. Only the full I-SFG space has it (68 ms; stage sequences give 76 ms).",
    "cfg": {
      "nodes": [
        {
          "name": "A",
          "cap": 40,
          "x": 90,
          "y": 170,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "B",
          "cap": 60,
          "x": 270,
          "y": 70,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "C",
          "cap": 50,
          "x": 300,
          "y": 170,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "D",
          "cap": 55,
          "x": 270,
          "y": 270,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "E",
          "cap": 40,
          "x": 510,
          "y": 170,
          "fault": 0,
          "inst": ""
        }
      ],
      "links": [
        {
          "a": 0,
          "b": 4,
          "delay": 18,
          "bw": 300
        },
        {
          "a": 0,
          "b": 1,
          "delay": 9,
          "bw": 300
        },
        {
          "a": 1,
          "b": 4,
          "delay": 12,
          "bw": 300
        },
        {
          "a": 0,
          "b": 2,
          "delay": 10,
          "bw": 300
        },
        {
          "a": 2,
          "b": 4,
          "delay": 14,
          "bw": 300
        },
        {
          "a": 0,
          "b": 3,
          "delay": 13,
          "bw": 300
        },
        {
          "a": 3,
          "b": 4,
          "delay": 15,
          "bw": 300
        },
        {
          "a": 1,
          "b": 2,
          "delay": 8,
          "bw": 300
        },
        {
          "a": 2,
          "b": 3,
          "delay": 9,
          "bw": 300
        }
      ],
      "src": 0,
      "dst": 4,
      "vnfs": [
        {
          "name": "f1",
          "level": 1,
          "proc": 9,
          "dem": 20,
          "fault": 0
        },
        {
          "name": "f2",
          "level": 2,
          "proc": 8,
          "dem": 15,
          "fault": 0
        },
        {
          "name": "f3",
          "level": 2,
          "proc": 8,
          "dem": 15,
          "fault": 0
        },
        {
          "name": "f4",
          "level": 2,
          "proc": 20,
          "dem": 25,
          "fault": 0
        },
        {
          "name": "f5",
          "level": 3,
          "proc": 10,
          "dem": 20,
          "fault": 0
        }
      ],
      "trep": 4,
      "tint": 4,
      "tmax": 200,
      "kmax": 1024,
      "rmin": 0,
      "alpha": 0,
      "candCap": 30000,
      "seed": true,
      "order": true,
      "compare": true,
      "breq": 50,
      "qcpu": 20,
      "instCap": 100,
      "wBw": 0.5,
      "roa": true,
      "pkt": 1,
      "isfg": "all",
      "full": true,
      "fullMs": 20000,
      "sync": true,
      "sMin": 0.5,
      "sMax": 1.5,
      "bufMax": 0
    }
  },
  {
    "label": "Preset R — reliability",
    "blurb": "Preset D with fault rates and R_min = 0.95: co-locating VNFs is no longer allowed.",
    "cfg": {
      "nodes": [
        {
          "name": "A",
          "cap": 40,
          "x": 90,
          "y": 170,
          "fault": 0.01,
          "inst": ""
        },
        {
          "name": "B",
          "cap": 60,
          "x": 270,
          "y": 70,
          "fault": 0.01,
          "inst": ""
        },
        {
          "name": "C",
          "cap": 50,
          "x": 300,
          "y": 170,
          "fault": 0.01,
          "inst": ""
        },
        {
          "name": "D",
          "cap": 55,
          "x": 270,
          "y": 270,
          "fault": 0.01,
          "inst": ""
        },
        {
          "name": "E",
          "cap": 40,
          "x": 510,
          "y": 170,
          "fault": 0.01,
          "inst": ""
        }
      ],
      "links": [
        {
          "a": 0,
          "b": 4,
          "delay": 18,
          "bw": 300
        },
        {
          "a": 0,
          "b": 1,
          "delay": 9,
          "bw": 300
        },
        {
          "a": 1,
          "b": 4,
          "delay": 12,
          "bw": 300
        },
        {
          "a": 0,
          "b": 2,
          "delay": 10,
          "bw": 300
        },
        {
          "a": 2,
          "b": 4,
          "delay": 14,
          "bw": 300
        },
        {
          "a": 0,
          "b": 3,
          "delay": 13,
          "bw": 300
        },
        {
          "a": 3,
          "b": 4,
          "delay": 15,
          "bw": 300
        },
        {
          "a": 1,
          "b": 2,
          "delay": 8,
          "bw": 300
        },
        {
          "a": 2,
          "b": 3,
          "delay": 9,
          "bw": 300
        }
      ],
      "src": 0,
      "dst": 4,
      "vnfs": [
        {
          "name": "f1",
          "level": 1,
          "proc": 9,
          "dem": 20,
          "fault": 0.01
        },
        {
          "name": "f2",
          "level": 2,
          "proc": 12,
          "dem": 25,
          "fault": 0.01
        },
        {
          "name": "f3",
          "level": 2,
          "proc": 15,
          "dem": 25,
          "fault": 0.01
        },
        {
          "name": "f4",
          "level": 3,
          "proc": 10,
          "dem": 30,
          "fault": 0.01
        }
      ],
      "trep": 2,
      "tint": 1,
      "tmax": 200,
      "kmax": 1024,
      "rmin": 0.95,
      "alpha": 0.04,
      "candCap": 30000,
      "seed": true,
      "order": true,
      "compare": true,
      "breq": 50,
      "qcpu": 20,
      "instCap": 100,
      "wBw": 0.5,
      "roa": true,
      "pkt": 1,
      "isfg": "all",
      "full": true,
      "fullMs": 20000,
      "sync": true,
      "sMin": 0.5,
      "sMax": 1.5,
      "bufMax": 0
    }
  },
  {
    "label": "Preset S — instance reuse",
    "blurb": "Preset D where C already runs f2 and f3, and E runs f4: ROA shares them, LOA goes for speed.",
    "cfg": {
      "nodes": [
        {
          "name": "A",
          "cap": 40,
          "x": 90,
          "y": 170,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "B",
          "cap": 60,
          "x": 270,
          "y": 70,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "C",
          "cap": 50,
          "x": 300,
          "y": 170,
          "fault": 0,
          "inst": "f2,f3"
        },
        {
          "name": "D",
          "cap": 55,
          "x": 270,
          "y": 270,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "E",
          "cap": 40,
          "x": 510,
          "y": 170,
          "fault": 0,
          "inst": "f4"
        }
      ],
      "links": [
        {
          "a": 0,
          "b": 4,
          "delay": 18,
          "bw": 300
        },
        {
          "a": 0,
          "b": 1,
          "delay": 9,
          "bw": 300
        },
        {
          "a": 1,
          "b": 4,
          "delay": 12,
          "bw": 300
        },
        {
          "a": 0,
          "b": 2,
          "delay": 10,
          "bw": 300
        },
        {
          "a": 2,
          "b": 4,
          "delay": 14,
          "bw": 300
        },
        {
          "a": 0,
          "b": 3,
          "delay": 13,
          "bw": 300
        },
        {
          "a": 3,
          "b": 4,
          "delay": 15,
          "bw": 300
        },
        {
          "a": 1,
          "b": 2,
          "delay": 8,
          "bw": 300
        },
        {
          "a": 2,
          "b": 3,
          "delay": 9,
          "bw": 300
        }
      ],
      "src": 0,
      "dst": 4,
      "vnfs": [
        {
          "name": "f1",
          "level": 1,
          "proc": 9,
          "dem": 20,
          "fault": 0
        },
        {
          "name": "f2",
          "level": 2,
          "proc": 12,
          "dem": 25,
          "fault": 0
        },
        {
          "name": "f3",
          "level": 2,
          "proc": 15,
          "dem": 25,
          "fault": 0
        },
        {
          "name": "f4",
          "level": 3,
          "proc": 10,
          "dem": 30,
          "fault": 0
        }
      ],
      "trep": 2,
      "tint": 1,
      "tmax": 200,
      "kmax": 1024,
      "rmin": 0,
      "alpha": 0,
      "candCap": 30000,
      "seed": true,
      "order": true,
      "compare": true,
      "breq": 50,
      "qcpu": 20,
      "instCap": 100,
      "wBw": 0.5,
      "roa": true,
      "pkt": 1,
      "isfg": "all",
      "full": true,
      "fullMs": 20000,
      "sync": true,
      "sMin": 0.5,
      "sMax": 1.5,
      "bufMax": 0
    }
  },
  {
    "label": "Preset L — larger network",
    "blurb": "8 nodes, 6 VNFs, 1.8 M DPG paths, 5 M schemes: paper 1 stops on its k limit, the full search still proves the optimum.",
    "cfg": {
      "nodes": [
        {
          "name": "A",
          "cap": 60,
          "x": 60,
          "y": 170,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "B",
          "cap": 45,
          "x": 190,
          "y": 60,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "C",
          "cap": 50,
          "x": 200,
          "y": 170,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "D",
          "cap": 45,
          "x": 190,
          "y": 280,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "E",
          "cap": 55,
          "x": 330,
          "y": 90,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "F",
          "cap": 50,
          "x": 340,
          "y": 250,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "G",
          "cap": 45,
          "x": 450,
          "y": 170,
          "fault": 0,
          "inst": ""
        },
        {
          "name": "H",
          "cap": 60,
          "x": 560,
          "y": 170,
          "fault": 0,
          "inst": ""
        }
      ],
      "links": [
        {
          "a": 0,
          "b": 1,
          "delay": 7,
          "bw": 300
        },
        {
          "a": 0,
          "b": 2,
          "delay": 9,
          "bw": 300
        },
        {
          "a": 0,
          "b": 3,
          "delay": 8,
          "bw": 300
        },
        {
          "a": 1,
          "b": 2,
          "delay": 5,
          "bw": 300
        },
        {
          "a": 2,
          "b": 3,
          "delay": 6,
          "bw": 300
        },
        {
          "a": 1,
          "b": 4,
          "delay": 8,
          "bw": 300
        },
        {
          "a": 2,
          "b": 4,
          "delay": 10,
          "bw": 300
        },
        {
          "a": 2,
          "b": 5,
          "delay": 9,
          "bw": 300
        },
        {
          "a": 3,
          "b": 5,
          "delay": 7,
          "bw": 300
        },
        {
          "a": 4,
          "b": 6,
          "delay": 6,
          "bw": 300
        },
        {
          "a": 5,
          "b": 6,
          "delay": 8,
          "bw": 300
        },
        {
          "a": 4,
          "b": 5,
          "delay": 11,
          "bw": 300
        },
        {
          "a": 6,
          "b": 7,
          "delay": 5,
          "bw": 300
        },
        {
          "a": 4,
          "b": 7,
          "delay": 14,
          "bw": 300
        },
        {
          "a": 5,
          "b": 7,
          "delay": 13,
          "bw": 300
        }
      ],
      "src": 0,
      "dst": 7,
      "vnfs": [
        {
          "name": "f1",
          "level": 1,
          "proc": 6,
          "dem": 25,
          "fault": 0
        },
        {
          "name": "f2",
          "level": 2,
          "proc": 14,
          "dem": 20,
          "fault": 0
        },
        {
          "name": "f3",
          "level": 2,
          "proc": 11,
          "dem": 20,
          "fault": 0
        },
        {
          "name": "f4",
          "level": 2,
          "proc": 9,
          "dem": 20,
          "fault": 0
        },
        {
          "name": "f5",
          "level": 3,
          "proc": 8,
          "dem": 30,
          "fault": 0
        },
        {
          "name": "f6",
          "level": 4,
          "proc": 5,
          "dem": 20,
          "fault": 0
        }
      ],
      "trep": 2,
      "tint": 2,
      "tmax": 300,
      "kmax": 4096,
      "candCap": 30000,
      "rmin": 0,
      "alpha": 0,
      "seed": true,
      "order": true,
      "compare": true,
      "breq": 50,
      "qcpu": 20,
      "instCap": 100,
      "wBw": 0.5,
      "roa": true,
      "pkt": 1,
      "isfg": "all",
      "full": true,
      "fullMs": 20000,
      "sync": true,
      "sMin": 0.5,
      "sMax": 1.5,
      "bufMax": 0
    }
  }
];

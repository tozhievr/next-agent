# Agent 0.45 measurements

Local sequential JSON-RPC, 100 C++ functions, 4,980 source bytes, 25 samples per warm operation. Cold runs use a new workspace; restart reuses the persisted derived outline. These measurements do not measure model tokens or compare agent products.

| Operation | Windows median | Linux/WSL2 median |
|---|---:|---:|
| Cold outline (single run) | 3,028 ms | 2,689 ms |
| Warm outline | 1.10 ms | 0.75 ms |
| Read one function | 0.85 ms | 0.52 ms |
| Edit + outline, 25 distinct versions | 67.17 ms | 15.46 ms |
| Outline after process restart (single run) | 28.67 ms | 188.02 ms |

Full-file response: 5,131 bytes; function response: 116 bytes (44.23 times smaller). Cold parsing remains a significant cost. Timings include Python transport and filesystem effects; Windows and WSL2 results are not a controlled OS comparison.

Raw reports: [Windows](agent-v045-windows.json), [Linux](agent-v045-linux.json). Reproduce with `python tools/benchmark_native.py`; use the Linux build under WSL for Linux measurements. Earlier 0.44 reports are historical baselines with a different benchmark setup.

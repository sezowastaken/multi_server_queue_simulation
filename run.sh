#!/usr/bin/env bash
set -e
mkdir -p reports logs

# Save current baseline
cp config/arrivals.csv config/.arrivals.bak
cp config/services.csv  config/.services.bak

# ---- Baseline ----
./out/sim 2 480
cp out/summary.csv reports/summary_baseline.csv
cp out/tick_log.csv logs/tick_baseline.csv

# ---- Arrival faster ----
cp config/arrivals_fast.csv config/arrivals.csv
cp config/.services.bak     config/services.csv
./out/sim 2 480
cp out/summary.csv reports/summary_arrival_fast.csv
cp out/tick_log.csv logs/tick_arrival_fast.csv

# ---- Service slower ----
cp config/.arrivals.bak     config/arrivals.csv
cp config/services_slow.csv config/services.csv
./out/sim 2 480
cp out/summary.csv reports/summary_service_slow.csv
cp out/tick_log.csv logs/tick_service_slow.csv

# ---- More servers (M=3) ----
cp config/.arrivals.bak     config/arrivals.csv
cp config/.services.bak     config/services.csv
./out/sim 3 480
cp out/summary.csv reports/summary_M3.csv
cp out/tick_log.csv logs/tick_M3.csv

# Restore baseline
mv -f config/.arrivals.bak config/arrivals.csv
mv -f config/.services.bak config/services.csv
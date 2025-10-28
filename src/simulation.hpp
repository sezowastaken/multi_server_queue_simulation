#pragma once
#include <vector>
#include <queue>
#include <map>
#include <random>
#include <string>
#include "distributions.hpp"

// --- Records ---
struct Customer {
    int id;
    int arrival = 0;
    int start   = -1;
    int svc     = 0;
    int depart  = -1;
};

struct Server {
    bool busy = false;
    int  busyUntil = 0;
    long long totalBusyTicks = 0; // per-tick kullanım için
    int served = 0;
};

enum class EvType { ARRIVAL, DEPARTURE };
struct Event { int time; EvType type; int custId; int serverId; };

// --- Config & Stats ---
struct SimConfig {
    int M = 2;                 // number of servers
    int horizon = 480;         // minutes to simulate
    int seed = 12345;
    bool writeLog = true;
    std::string tickLogCsv = "out/tick_log.csv"; // per-tick
    std::string summaryCsv = "out/summary.csv";  // run summary
};

struct SimStats {
    int arrived   = 0;
    int completed = 0;
    long long sumWait = 0;
    long long sumSvc  = 0;
    long long sumSys  = 0;
    int maxQ = 0;
    double avgQ = 0.0;
    double utilAvg = 0.0; // avg busy servers / M
};

// --- Simulation core ---
class Simulation {
public:
    Simulation(const SimConfig& cfg, const DiscreteDist& inter, const DiscreteDist& svc);
    void run();
    const SimStats& stats() const { return st; }

private:
    // input
    SimConfig cfg;
    const DiscreteDist& ARR;
    const DiscreteDist& SVC;

    // state
    std::mt19937 gen;
    int clock = 0;
    std::vector<Server> servers;
    std::vector<Customer> cust;
    std::queue<int> Q;
    std::multimap<int, Event> FEL;

    // stats
    SimStats st;
    long long queueLenIntegral = 0; // ∑ qlen per tick
    long long busyServersIntegral = 0; // ∑ busyCount per tick

    // helpers
    void scheduleNextArrival(int now);
    void handleDeparturesAt(int t, int& deps);
    void serveWaitingIfPossible(int t, int& starts);
    void handleArrivalsAt(int t, int& arrs);
    void tickStats();
    int  countBusy() const;
};

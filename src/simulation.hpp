#pragma once
#include <vector>
#include <queue>
#include <map>
#include <random>
#include <string>
#include "distributions.hpp"

// --- Basic records ---
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
    long long totalBusy = 0;
    int served = 0;
};

enum class EvType { ARRIVAL, DEPARTURE };
struct Event { int time; EvType type; int custId; int serverId; };

// --- Config & Stats ---
struct SimConfig {
    int M = 2;          // number of servers
    int horizon = 480;  // minutes to simulate
    int seed = 12345;
    bool writeLog = true;
    std::string logPath = "out/log.txt";
};

struct SimStats {
    int arrived   = 0;
    int completed = 0;
    long long sumWait = 0;
    long long sumSvc  = 0;
    long long sumSys  = 0;
    int maxQ = 0;
    double avgQ = 0.0;      // time-averaged queue length
    double utilAvg = 0.0;   // average utilization over all servers
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
    std::vector<Customer> cust;     // record for KPIs
    std::queue<int> Q;              // waiting list (FIFO)
    std::multimap<int, Event> FEL;  // future events, keyed by time

    // stats
    SimStats st;
    long long queueLenIntegral = 0; // sum of queue length per tick

    // helpers
    void scheduleNextArrival(int now);
    void handleDeparturesAt(int t);
    void serveWaitingIfPossible(int t);
    void handleArrivalsAt(int t);
    void tickStats();
};

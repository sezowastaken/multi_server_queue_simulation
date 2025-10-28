#include <iostream>
#include <cstdlib>
#include "distributions.hpp"
#include "simulation.hpp"
using namespace std;

int main(int argc, char* argv[]) {
    try {
        auto arrival = DiscreteDist::fromCsv("config/arrivals.csv");
        auto service = DiscreteDist::fromCsv("config/services.csv");

        SimConfig cfg;
        cfg.writeLog = true;

        if (argc >= 2) cfg.M = atoi(argv[1]);
        if (argc >= 3) cfg.horizon = atoi(argv[2]);

        if (cfg.M <= 0) {
            cerr << "ERROR: Server count must be >= 1\n";
            return 2;
        }
        if (cfg.horizon <= 0) {
            cerr << "ERROR: Horizon must be >= 1\n";
            return 3;
        }

        Simulation sim(cfg, arrival, service);
        sim.run();

        const auto& st = sim.stats();

        cout << "Servers   : " << cfg.M << "\n";
        cout << "Horizon   : " << cfg.horizon << " min\n";
        cout << "Completed : " << st.completed << "\n";

        if (st.completed > 0) {
            cout << "Avg Wait  : " << (double)st.sumWait / st.completed << " min\n";
            cout << "Avg Svc   : " << (double)st.sumSvc  / st.completed << " min\n";
            cout << "Avg Sys   : " << (double)st.sumSys  / st.completed << " min\n";
        }

        cout << "MaxQ      : " << st.maxQ << "\n";
        cout << "AvgQ      : " << st.avgQ << "\n";
        cout << "UtilAvg   : " << st.utilAvg * 100.0 << "%\n";
        cout << "P(Wait>0) : " << st.pWait * 100.0 << "%\n";
        cout << "Log files : out/tick_log.csv, out/summary.csv\n";
    }
    catch (const exception& e) {
        cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
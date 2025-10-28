#include <iostream>
#include "distributions.hpp"
#include "simulation.hpp"
using namespace std;

int main(){
    try{
        auto arrival = DiscreteDist::fromCsv("config/arrivals.csv");
        auto service = DiscreteDist::fromCsv("config/services.csv");

        SimConfig cfg;    // defaults: M=2, horizon=480, seed=12345
        cfg.writeLog = true;

        Simulation sim(cfg, arrival, service);
        sim.run();

        const auto& st = sim.stats();
        cout << "Completed : " << st.completed << "\n";
        if(st.completed>0){
            cout << "Avg Wait  : " << (double)st.sumWait / st.completed << " min\n";
            cout << "Avg Svc   : " << (double)st.sumSvc  / st.completed << " min\n";
            cout << "Avg System: " << (double)st.sumSys  / st.completed << " min\n";
        }
        cout << "MaxQ      : " << st.maxQ  << "\n";
        cout << "AvgQ      : " << st.avgQ  << "\n";
        cout << "UtilAvg   : " << st.utilAvg*100.0 << "%\n";
        cout << "Logs at   : out/tick_log.csv, out/summary.csv\n";
    }catch(const std::exception& e){
        cerr << "ERROR: " << e.what() << "\n"; return 1;
    }
    return 0;
}

#include "simulation.hpp"
#include <fstream>
#include <iostream>

Simulation::Simulation(const SimConfig& cfg, const DiscreteDist& inter, const DiscreteDist& svc)
: cfg(cfg), ARR(inter), SVC(svc), gen(cfg.seed) {
    servers.assign(cfg.M, {});
    // first arrival at time = sample interarrival from 0
    int first = ARR.sample(gen);
    FEL.insert({first, Event{first, EvType::ARRIVAL, -1, -1}});
}

void Simulation::scheduleNextArrival(int now){
    int gap = ARR.sample(gen);
    int t = now + gap;
    if (t <= cfg.horizon) {
        FEL.insert({t, Event{t, EvType::ARRIVAL, -1, -1}});
    }
}

void Simulation::handleDeparturesAt(int t){
    auto range = FEL.equal_range(t);
    std::vector<std::multimap<int,Event>::iterator> dels;

    for (auto it = range.first; it != range.second; ++it) {
        const Event& e = it->second;
        if (e.type == EvType::DEPARTURE) {
            // free server and finalize customer
            Server& s = servers[e.serverId];
            s.busy = false;
            // add busy contribution (service time) safely
            s.totalBusy += (s.busyUntil > t ? s.busyUntil - t : 0); // minimal guard
            Customer& c = cust[e.custId];
            c.depart = t;

            st.completed++;
            st.sumSvc  += c.svc;
            st.sumWait += (c.start - c.arrival);
            st.sumSys  += (c.depart - c.arrival);

            dels.push_back(it);
        }
    }
    for (auto it : dels) FEL.erase(it);
}

void Simulation::serveWaitingIfPossible(int t){
    while (!Q.empty()) {
        int idle = -1;
        for (int i = 0; i < (int)servers.size(); ++i) if (!servers[i].busy) { idle = i; break; }
        if (idle == -1) break;

        int cid = Q.front(); Q.pop();
        Customer& c = cust[cid];
        c.start = t;
        c.svc   = SVC.sample(gen);
        c.depart= t + c.svc;

        Server& s = servers[idle];
        s.busy = true;
        s.busyUntil = c.depart;
        s.served++;

        FEL.insert({c.depart, Event{c.depart, EvType::DEPARTURE, cid, idle}});
    }
}

void Simulation::handleArrivalsAt(int t){
    auto range = FEL.equal_range(t);
    std::vector<std::multimap<int,Event>::iterator> dels;

    for (auto it = range.first; it != range.second; ++it) {
        const Event& e = it->second;
        if (e.type == EvType::ARRIVAL) {
            // create new customer
            Customer c; c.id = (int)cust.size(); c.arrival = t;
            cust.push_back(c);
            st.arrived++;

            // try to assign immediately, else queue
            int idle = -1;
            for (int i = 0; i < (int)servers.size(); ++i) if (!servers[i].busy) { idle = i; break; }

            if (idle != -1 && Q.empty()) {
                // direct start
                Customer& r = cust.back();
                r.start = t;
                r.svc   = SVC.sample(gen);
                r.depart= t + r.svc;

                Server& s = servers[idle];
                s.busy = true; s.busyUntil = r.depart; s.served++;
                FEL.insert({r.depart, Event{r.depart, EvType::DEPARTURE, r.id, idle}});
            } else {
                Q.push(c.id);
                if ((int)Q.size() > st.maxQ) st.maxQ = (int)Q.size();
            }

            // schedule the next arrival after handling this one
            scheduleNextArrival(t);
            dels.push_back(it);
        }
    }
    for (auto it : dels) FEL.erase(it);
}

void Simulation::tickStats(){
    // time-averaged queue length (per-minute integral)
    queueLenIntegral += (long long)Q.size();
}

void Simulation::run(){
    // Simple per-minute loop until horizon; then drain remaining FEL/Q
    for (clock = 0; ; ++clock) {
        // 1) departures first
        handleDeparturesAt(clock);

        // 2) if queue non-empty, try to start service
        serveWaitingIfPossible(clock);

        // 3) arrivals at this minute
        handleArrivalsAt(clock);

        // 4) per-tick stats
        tickStats();

        bool pastHorizon = (clock >= cfg.horizon);
        bool nothingLeft = FEL.empty() && Q.empty();
        if (pastHorizon && nothingLeft) break;
    }

    // finalize aggregate KPIs
    st.avgQ = (clock > 0) ? (double)queueLenIntegral / (double)clock : 0.0;

    long long sumBusy = 0;
    for (const auto& s : servers) sumBusy += s.served > 0 ? (long long)s.served /*approx*/ : 0;
    // TODO: For accurate utilization, count per-tick busy status; this is an approximation placeholder
    st.utilAvg = 0.0; // Empty for now; will be updated with per-tick busy status in next phase

    // optional log
    if (cfg.writeLog) {
        std::ofstream out(cfg.logPath, std::ios::app);
        out << "RUN FINISHED\n"
            << "Arrived=" << st.arrived
            << " Completed=" << st.completed
            << " AvgWait=" << (st.completed? (double)st.sumWait/st.completed:0.0)
            << " AvgSvc="  << (st.completed? (double)st.sumSvc /st.completed:0.0)
            << " AvgSys="  << (st.completed? (double)st.sumSys /st.completed:0.0)
            << " MaxQ="    << st.maxQ
            << " AvgQ="    << st.avgQ
            << "\n\n";
    }
}

#include "simulation.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>

Simulation::Simulation(const SimConfig& cfg,
                       const DiscreteDist& inter,
                       const DiscreteDist& svc)
: cfg(cfg), ARR(inter), SVC(svc), gen(cfg.seed)
{
    servers.assign(cfg.M, {});
    int first = ARR.sample(gen);
    FEL.insert({first, Event{first, EvType::ARRIVAL, -1, -1}});
}

void Simulation::scheduleNextArrival(int now){
    int gap = ARR.sample(gen);
    int t = now + gap;
    if (t <= cfg.horizon) FEL.insert({t, Event{t, EvType::ARRIVAL, -1, -1}});
}

void Simulation::handleDeparturesAt(int t, int& deps){
    auto range = FEL.equal_range(t);
    std::vector<std::multimap<int,Event>::iterator> dels;

    for (auto it = range.first; it != range.second; ++it) {
        const Event& e = it->second;
        if (e.type == EvType::DEPARTURE) {
            Server& s = servers[e.serverId];
            s.busy = false;
            Customer& c = cust[e.custId];
            c.depart = t;

            st.completed++;
            st.sumSvc  += c.svc;
            st.sumWait += (c.start - c.arrival);
            st.sumSys  += (c.depart - c.arrival);
            deps++;

            dels.push_back(it);
        }
    }
    for (auto it : dels) FEL.erase(it);
}

void Simulation::serveWaitingIfPossible(int t, int& starts){
    while (!Q.empty()) {
        int idle = -1;
        for (int i = 0; i < (int)servers.size(); ++i)
            if (!servers[i].busy) { idle = i; break; }
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
        starts++;

        FEL.insert({c.depart, Event{c.depart, EvType::DEPARTURE, cid, idle}});
    }
}

void Simulation::handleArrivalsAt(int t, int& arrs, int& starts){
    auto range = FEL.equal_range(t);
    std::vector<std::multimap<int,Event>::iterator> dels;

    for (auto it = range.first; it != range.second; ++it) {
        const Event& e = it->second;
        if (e.type == EvType::ARRIVAL) {
            Customer c; c.id = (int)cust.size(); c.arrival = t;
            cust.push_back(c);
            st.arrived++; arrs++;

            int idle = -1;
            for (int i = 0; i < (int)servers.size(); ++i)
                if (!servers[i].busy) { idle = i; break; }

            if (idle != -1 && Q.empty()) {
                Customer& r = cust.back();
                r.start = t;
                r.svc   = SVC.sample(gen);
                r.depart= t + r.svc;

                Server& s = servers[idle];
                s.busy = true; s.busyUntil = r.depart; s.served++;
                FEL.insert({r.depart, Event{r.depart, EvType::DEPARTURE, r.id, idle}});

                starts++;  // count immediate starts for logging consistency
            } else {
                Q.push(c.id);
                st.waited++;
                if ((int)Q.size() > st.maxQ) st.maxQ = (int)Q.size();
            }

            scheduleNextArrival(t);
            dels.push_back(it);
        }
    }
    for (auto it : dels) FEL.erase(it);
}

int Simulation::countBusy() const{
    int b=0;
    for (const auto& s: servers) if (s.busy) ++b;
    return b;
}

void Simulation::tickStats(){
    queueLenIntegral    += (long long)Q.size();
    busyServersIntegral += (long long)countBusy();
    for (auto& s: servers) if (s.busy) s.totalBusyTicks++;
    ++ticks; 
}

void Simulation::run(){
    std::ofstream tickLog;
    if (cfg.writeLog) {
        tickLog.open(cfg.tickLogCsv, std::ios::trunc);
        tickLog << "t,arrivals,starts,departures,queue_len,busy_servers\n";
    }

    for (clock = 0; ; ++clock) {
        int deps=0, starts=0, arrs=0;

        handleDeparturesAt(clock, deps);
        serveWaitingIfPossible(clock, starts);
        handleArrivalsAt(clock, arrs, starts);

        tickStats();
        if (cfg.writeLog) {
            tickLog << clock << "," << arrs << "," << starts << "," << deps
                    << "," << Q.size() << "," << countBusy() << "\n";
        }

        bool pastHorizon = (clock >= cfg.horizon);
        bool done = FEL.empty() && Q.empty();
        if (pastHorizon && done) break;
    }
    if (cfg.writeLog) tickLog.close();

st.avgQ    = (ticks>0) ? (double)queueLenIntegral / ticks : 0.0;
st.utilAvg = (ticks>0) ? (double)busyServersIntegral / ((double)cfg.M * (double)ticks) : 0.0;
    st.pWait   = (st.completed>0) ? (double)st.waited / (double)st.completed : 0.0;

    if (cfg.writeLog) {
        std::ofstream sum(cfg.summaryCsv, std::ios::trunc);
        sum << "arrived,completed,avg_wait,avg_service,avg_system,max_q,avg_q,avg_util,p_wait\n";
        double aw = st.completed? (double)st.sumWait/st.completed : 0.0;
        double as = st.completed? (double)st.sumSvc /st.completed : 0.0;
        double ay = st.completed? (double)st.sumSys /st.completed : 0.0;
        sum << st.arrived << "," << st.completed << ","
            << aw << "," << as << "," << ay << ","
            << st.maxQ << "," << st.avgQ << "," << st.utilAvg << ","
            << st.pWait << "\n";
    }
}
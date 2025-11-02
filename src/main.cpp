#include <iostream>
#include <list>
#include <vector>
#include <queue>
#include <random>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <cctype>
#include <map>
#include <climits>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>

using namespace std;

//
struct ArrivalData {
    int time;
    double prob;
};

struct CumulativeData {
    int time;
    double cumulative_prob;
};

struct Customer {
    int id;
    int arrival_time;
};

enum EventType { ARRIVAL, DEPARTURE };
struct Event {
    EventType type;
    int time;
    int customer_id;
    string server_name;

    bool operator>(const Event& other) const {
        return time > other.time;
    }
};

enum ServerStatus { IDLE, BUSY };
struct Server {
    string name;
    ServerStatus status = IDLE;
    int current_customer_id = -1;
};

struct SimulationStats {
    double total_wait_time;
    int customers_who_waited;
    int max_queue_length;
    int simulation_end_time;
    
    map<string, int> server_total_busy_time;
};


static inline string trim(const string& s) {
    size_t b = 0, e = s.size();
    while (b < e && isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && isspace(static_cast<unsigned char>(s[e-1]))) --e;
    return s.substr(b, e - b);
}

// ------- ARRIVAL (inter-arrival) -------
pair<vector<ArrivalData>, double> readArrivalDataFromCSV(const string& filename) {
    vector<ArrivalData> raw_data;
    double prob_sum = 0.0;
    ifstream file(filename);

    if (!file.is_open()) {
        cerr << "ERROR: cannot open file " << filename << endl;
        exit(1);
    }

    string line;
    if (!getline(file, line)) {
        cerr << "ERROR: empty file " << filename << endl;
        exit(1);
    }

    while (getline(file, line)) {
        line = trim(line);
        if (line.empty()) continue;
        stringstream ss(line);
        string seg;
        getline(ss, seg, ',');
        int t = stoi(trim(seg));
        getline(ss, seg, ',');
        double prob = stod(trim(seg));
        prob_sum += prob;
        raw_data.push_back({t, prob});
    }
    return {raw_data, prob_sum};
}

vector<CumulativeData> calculateCumulative(const vector<ArrivalData>& data) {
    vector<CumulativeData> out;
    out.reserve(data.size());
    double sum = 0.0;
    for (const auto& it : data) {
        sum += it.prob;
        out.push_back({it.time, sum});
    }
    if (!out.empty()) {
        if (sum > 0.999999 && sum < 1.000001) out.back().cumulative_prob = 1.0;
    }
    return out;
}

int generateInterArrivalTime(const vector<CumulativeData>& cum, mt19937& gen) {
    uniform_real_distribution<> dis(0.0, 1.0);
    double R = dis(gen);
    for (const auto& it : cum) if (R <= it.cumulative_prob) return it.time;
    return cum.back().time;
}

vector<int> fillArrivalList(int total_events, const vector<ArrivalData>& raw_data, mt19937& gen) {
    auto cum = calculateCumulative(raw_data);
    vector<int> result;
    for (int i = 0; i < total_events; ++i) {
        result.push_back(generateInterArrivalTime(cum, gen));
    }
    return result;
}

vector<int> createArrivalTimes(const vector<int>& inter_arrival_times) {
    vector<int> arrival_times;
    if (inter_arrival_times.empty()) {
        return arrival_times;
    }

    arrival_times.push_back(0); 
    int current_time = 0;

    for (size_t i = 1; i < inter_arrival_times.size(); ++i) {
        current_time += inter_arrival_times[i];
        arrival_times.push_back(current_time);
    }
    return arrival_times;
}

// ----------------- SERVICE -----------------
struct ServerSpec {
    string name;
    vector<ArrivalData> dist;          // (time, prob)
    vector<CumulativeData> cumulative;
    double prob_sum;
};

unordered_map<string, ServerSpec> readServiceDataFromCSV(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "ERROR: cannot open file " << filename << endl;
        exit(1);
    }

    unordered_map<string, ServerSpec> servers;
    string current;

    string line;
    bool firstLine = true;
    while (getline(file, line)) {
        line = trim(line);
        if (line.empty()) continue;

        if (firstLine) {
            firstLine = false;
            string lower = line;
            for (auto& ch : lower) ch = tolower(static_cast<unsigned char>(ch));
            if (lower == "time,prob" || lower == "time;prob") continue;
        }

        if (line.find(',') == string::npos) {
            current = line;
            if (servers.find(current) == servers.end()) {
                servers[current] = ServerSpec{current, {}, {}, 0.0};
            }
            continue;
        }

        if (current.empty()) {
            cerr << "ERROR: data row encountered before any server name in " << filename << endl;
            exit(1);
        }

        stringstream ss(line);
        string seg1, seg2;
        if (!getline(ss, seg1, ',')) continue;
        if (!getline(ss, seg2, ',')) continue;
        int t = stoi(trim(seg1));
        double p = stod(trim(seg2));
        
        servers[current].dist.push_back({t, p});
        servers[current].prob_sum += p;
    }

    for (auto& kv : servers) {
        if (kv.second.dist.empty()) {
            cerr << "ERROR: server '" << kv.first << "' has no (time,prob) rows.\n";
            exit(1);
        }
    }
    return servers;
}

void buildCumulativeForServers(unordered_map<string, ServerSpec>& servers) {
    for (auto& kv : servers) {
        kv.second.cumulative = calculateCumulative(kv.second.dist);
        if (kv.second.cumulative.empty() || kv.second.cumulative.back().cumulative_prob < 0.999) {
            cerr << "WARNING: probabilities of server '" << kv.first
                 << "' do not sum ~1. Last cum=" << kv.second.cumulative.back().cumulative_prob << "\n";
        }
    }
}

vector<int> generateServiceTimesPerServer(const ServerSpec& s, int customers, mt19937& gen) {
    vector<int> out;
    out.reserve(customers);
    uniform_real_distribution<> dis(0.0, 1.0);
    for (int i = 0; i < customers; ++i) {
        double R = dis(gen);
        int sample = s.cumulative.back().time;
        for (const auto& c : s.cumulative) {
            if (R <= c.cumulative_prob) { sample = c.time; break; }
        }
        out.push_back(sample);
    }
    return out;
}

//declaring body of the logic methods
void handle_departures(int clock, list<Event>& FEL, map<string, Server>& server_states, SimulationStats& stats, ofstream& log_file);
void handle_waiting_list(int clock, queue<Customer>& waiting_list, map<string, Server>& server_states,
                         list<Event>& FEL, const unordered_map<string, vector<int>>& service_samples,
                         SimulationStats& stats, ofstream& log_file);
void handle_arrivals(int clock, list<Event>& FEL, queue<Customer>& waiting_list, 
                     map<string, Server>& server_states, const unordered_map<string,
                     vector<int>>& service_samples, SimulationStats& stats, ofstream& log_file);

// ----------------------------------- main -----------------------------------
int main() {
    mt19937 gen(42); //her seferinde sabit randomları üretiyor debug için uygun sonra değişmesi lazım

    const string ARRIVALS_FILE = "config/arrivals.csv";
    const string SERVICES_FILE = "config/services.csv";

    auto arrival_result = readArrivalDataFromCSV(ARRIVALS_FILE);
    vector<ArrivalData> raw_arrival_data = arrival_result.first;
    double arrival_prob_sum = arrival_result.second;

    auto servers = readServiceDataFromCSV(SERVICES_FILE);

    bool probabilities_are_valid = true;
    const double EPSILON = 0.001;

    if (abs(arrival_prob_sum - 1.0) > EPSILON) {
        cerr << "FATAL VALIDATION ERROR in '" << ARRIVALS_FILE << "':" << endl;
        cerr << "  Probabilities must sum to 1.0, but they sum to " 
             << fixed << setprecision(4) << arrival_prob_sum << "." << endl;
        probabilities_are_valid = false;
    }

    for (const auto& kv : servers) {
        string server_name = kv.first;
        double server_prob_sum = kv.second.prob_sum;
        if (abs(server_prob_sum - 1.0) > EPSILON) {
            cerr << "FATAL VALIDATION ERROR in '" << SERVICES_FILE << "' (Server: " << server_name << "):" << endl;
            cerr << "  Probabilities must sum to 1.0, but they sum to "
                 << fixed << setprecision(4) << server_prob_sum << "." << endl;
            probabilities_are_valid = false;
        }
    }

    if (!probabilities_are_valid) {
        cerr << "\nPlease fix the probability values in the .csv files and try again." << endl;
        return 1;
    }

    // --- Logs ---

    auto now = chrono::system_clock::now();
    auto in_time_t = chrono::system_clock::to_time_t(now);
    
    stringstream ss_time;
    ss_time << put_time(localtime(&in_time_t), "%Y%m%d_%H%M%S");
    string log_filename = "logs/sim_log_" + ss_time.str() + ".log";

    ofstream log_file(log_filename);
    if (!log_file.is_open()) {
        cerr << "FATAL ERROR: Could not create log file at: " << log_filename << endl;
        cerr << "Please make sure the 'logs' directory exists." << endl;
        return 1;
    }

    //----------------------------------- Phase-1 Initialize -----------------------------------

    cout << "Multi-Server Queue Simulation - CMPE 412\n\n";
    cout << "Logging all output to: " << log_filename << "\n" << endl;
    log_file << "Multi-Server Queue Simulation - CMPE 412\n\n";

    int customers;
    cout << "How many customers: ";
    cin >> customers;
    log_file << "Customer size: " << customers << "\n\n";

    vector<int> inter_arrival_times = fillArrivalList(customers, raw_arrival_data, gen);
    auto arrival_times = createArrivalTimes(inter_arrival_times);

    // builds multiple servers from services.csv
    buildCumulativeForServers(servers);
    // create a service lists that is long as customer amount for every server
    unordered_map<string, vector<int>> service_samples;
    for (const auto& kv : servers) {
        service_samples[kv.first] = generateServiceTimesPerServer(kv.second, customers, gen);
    }

    stringstream ss_setup;

    ss_setup << "Generated Arrival Times (t=0):" << endl;
    for(size_t i = 0; i < arrival_times.size(); ++i) {
        ss_setup << "Cst " << left << setw(4) << i+1 
             << "t=" << right << setw(4) << arrival_times[i];
        
        if ( (i+1) % 8 == 0 || i == arrival_times.size() - 1) {
            ss_setup << "\n";
        } else {
            ss_setup << " | "; 
        }
    }
    ss_setup << "\n";

    ss_setup << "Service Times for Each Server" << endl;
    for (const auto& kv : service_samples){
        ss_setup << "\nServer: " << kv.first << endl;
        ss_setup << "---------------------------" << endl;
        
        for(size_t i = 0; i < kv.second.size(); ++i) {
            ss_setup << "Cst " << left << setw(4) << i+1 
                 << "s=" << right << setw(2) << kv.second[i];
            
            if ( (i+1) % 8 == 0 || i == kv.second.size() - 1) {
                ss_setup << "\n";
            } else {
                ss_setup << " | ";
            }
        }
    }
    ss_setup << endl;

    cout << ss_setup.str();
    log_file << ss_setup.str();

    //----------------------------------- Phase-2 Simulation Start -----------------------------------

    cout << "\n\n--- SIMULATION STARTING ---" << endl;
    log_file << "\n\n--- SIMULATION STARTING ---" << endl;

    list<Event> FEL; // Future Event List (contains events to happen like customer Arrival or customer Departure)
    queue<Customer> waiting_list;   //customer's waiting line
    map<string, Server> server_states; //server's current state (IDLE or BUSY)

    SimulationStats stats;
    stats.total_wait_time = 0;
    stats.customers_who_waited = 0;
    stats.max_queue_length = 0;
    stats.simulation_end_time = 0;

    for (const auto& server : servers) {
        string server_name = server.first;
        server_states[server_name] = Server{server_name, IDLE, -1};
    }

    for(unsigned int i = 0; i < arrival_times.size(); ++i){
        int customer_id = i;
        int customer_arrival_time = arrival_times[i];

        Event arrival_event{ARRIVAL, customer_arrival_time, customer_id, ""};
        FEL.push_back(arrival_event);
    }

    cout << "  [SETUP] FEL initialized with " << FEL.size() << " ARRIVAL events." << endl;
    log_file << "  [SETUP] FEL initialized with " << FEL.size() << " ARRIVAL events." << endl;
    cout << "  [SETUP] All " << server_states.size() << " servers initialized as IDLE." << endl;
    log_file << "  [SETUP] All " << server_states.size() << " servers initialized as IDLE." << endl;

    int clock = 0;

    while (!FEL.empty()) {

        if ((!FEL.empty() && FEL.front().time == clock) || !waiting_list.empty()) {
             cout << "\n--- [CLOCK: " << clock << "] ---" << endl;
             log_file << "\n--- [CLOCK: " << clock << "] ---" << endl;
        } else {
             log_file << "\n--- [CLOCK: " << clock << "] ---" << endl;
             log_file << "  (No events scheduled, queue is empty.)" << endl;
        }

        // -------------------------------------------------
        // 
        handle_departures(clock, FEL, server_states, stats, log_file);
        handle_waiting_list(clock, waiting_list, server_states, FEL, service_samples, stats, log_file);
        handle_arrivals(clock, FEL, waiting_list, server_states, service_samples, stats, log_file);
        //
        // -------------------------------------------------


        clock++;
        

        if (clock > 99999) {
            cout << "  [WARN] Simulation limit reached (99999). Breaking loop." << endl;
            log_file << "  [WARN] Simulation limit reached (99999). Breaking loop." << endl;
            break;
        }
    }

    //----------------------------------- Phase-3 KPI's -----------------------------------

    stringstream ss_kpi;

    ss_kpi << "\n--- SIMULATION END ---" << endl;
    ss_kpi << "Simulation finished at clock: " << stats.simulation_end_time << endl;

    ss_kpi << "\n=============================================" << endl;
    ss_kpi << "--- SIMULATION STATISTICS & KPIs ---" << endl;
    ss_kpi << "=============================================\n" << endl;

    ss_kpi << "--- General Statistics ---" << endl;
    ss_kpi << "  Total simulation time:     " << stats.simulation_end_time << " minutes" << endl;
    ss_kpi << "  Total customers processed: " << customers << endl;
    ss_kpi << "  Customers who waited:      " << stats.customers_who_waited << endl;
    ss_kpi << "  Maximum queue length:      " << stats.max_queue_length << " customers" << endl;
    
    ss_kpi << "\n--- Wait Time KPIs ---" << endl;
    ss_kpi << "  Total wait time:           " << fixed << setprecision(2) << stats.total_wait_time << " minutes" << endl;
    
    double avg_wait_time_all = stats.total_wait_time / customers;
    ss_kpi << "  Avg. wait time (all cst):  " << fixed << setprecision(2) << avg_wait_time_all << " minutes" << endl;

    if (stats.customers_who_waited > 0) {
        double avg_wait_time_waited = stats.total_wait_time / stats.customers_who_waited;
        ss_kpi << "  Avg. wait time (waiting cst):" << fixed << setprecision(2) << avg_wait_time_waited << " minutes" << endl;
    } else {
        ss_kpi << "  Avg. wait time (waiting cst): 0.00 minutes (No customers waited)" << endl;
    }

    ss_kpi << "\n--- Server Utilization ---" << endl;
    
    for (auto const& pair : server_states) {
        string server_name = pair.first;
        
        int busy_time = stats.server_total_busy_time[server_name];
        
        double utilization = 0.0;
        if (stats.simulation_end_time > 0) {
            utilization = (double)busy_time / stats.simulation_end_time * 100.0;
        }

        ss_kpi << "  Server [" << setw(8) << left << server_name << "]: " 
             << "Busy for " << setw(4) << right << busy_time << " min. "
             << "(Utilization: " << fixed << setprecision(2) << utilization << "%)" << endl;
    }
    
    ss_kpi << "\n=============================================" << endl;

    cout << ss_kpi.str();
    log_file << ss_kpi.str();

    log_file.close();

    return 0;
}

void handle_departures(int clock, list<Event>& FEL, map<string, Server>& server_states, SimulationStats& stats, ofstream& log_file) {
    

    //iterate FEL
    for (list<Event>::iterator it = FEL.begin(); it != FEL.end();) {

        //if we ahead of clock time end the loop (this shouldn't happen but just in case)
        if (it->time > clock) {
            break; 
        }

        //if clock time is same as the FEL's departure event time
        if (it->time == clock && it->type == DEPARTURE) {
            
            cout << "  DEPARTURE: Customer " << it->customer_id + 1
                 << " finished service at server '" << it->server_name << "'." << endl;
            log_file << "  DEPARTURE: Customer " << it->customer_id + 1
                 << " finished service at server '" << it->server_name << "'." << endl;
            
            stats.simulation_end_time = clock;

            //take the served server
            Server& server = server_states[it->server_name];
            
            //set it to idle again
            server.status = IDLE;
            server.current_customer_id = -1;
            
            //we handled the departure event so we can delete it from the FEL
            it = FEL.erase(it);

        } else {
            ++it;
        }
    }
}

void handle_waiting_list(int clock, 
                         queue<Customer>& waiting_list, 
                         map<string, Server>& server_states, 
                         list<Event>& FEL,
                         const unordered_map<string, vector<int>>& service_samples,
                         SimulationStats& stats,
                         ofstream& log_file) 
{
    // Keep assigning customers as long as the queue is not empty
    // AND we can find available servers for them.
    while (!waiting_list.empty()) {

        // Peek at the customer at the front of the queue
        Customer& customer_to_serve = waiting_list.front();
        int customer_id = customer_to_serve.id;

        // Find the fastest available (IDLE) server
        Server* best_server = nullptr;
        int min_service_time = INT_MAX;

        // Iterate through ALL servers to find the best fit
        for (auto& pair : server_states) {
            Server& current_server = pair.second;
            
            if (current_server.status == IDLE) {
                // Check the service time for this specific customer
                int potential_service_time = service_samples.at(current_server.name)[customer_id];
                
                // If it's the best so far, save it
                if (potential_service_time < min_service_time) {
                    min_service_time = potential_service_time;
                    best_server = &current_server;
                }
            }
        }

        // Did we find an available server?
        if (best_server != nullptr) {
            // Yes, assign customer to the 'best_server'

            waiting_list.pop(); // Now remove customer from queue

            // calculate stats
            int wait_time = clock - customer_to_serve.arrival_time;
            stats.total_wait_time += wait_time;
            stats.customers_who_waited++;

            int service_time = min_service_time;
            int finish_time = clock + service_time;

            stats.server_total_busy_time[best_server->name] += service_time;

            // set server to busy
            best_server->status = BUSY;
            best_server->current_customer_id = customer_id; 

            Event departure_event{DEPARTURE, finish_time, customer_id, best_server->name};

            // adding departure event to FEL
            auto it = FEL.begin();
            while (it != FEL.end() && it->time < departure_event.time) { 
                ++it;
            }
            FEL.insert(it, departure_event);

            cout << "  QUEUE->SERVER: Customer " << customer_id + 1 
                 << " (waited " << wait_time << " min) assigned to (Best fit) server '" << best_server->name << "'."
                 << " Service time: " << service_time << " min."
                 << " (Finishes at t=" << finish_time << ")" << endl;
            log_file << "  QUEUE->SERVER: Customer " << customer_id + 1 
                 << " (waited " << wait_time << " min) assigned to (Best fit) server '" << best_server->name << "'."
                 << " Service time: " << service_time << " min."
                 << " (Finishes at t=" << finish_time << ")" << endl;

        } else {
            // No IDLE servers were found.
            break; 
        }
    }
}

void handle_arrivals(int clock, 
                     list<Event>& FEL, 
                     queue<Customer>& waiting_list, 
                     map<string, Server>& server_states,
                     const unordered_map<string, vector<int>>& service_samples,
                     SimulationStats& stats, 
                     ofstream& log_file) 
{
    for (auto it = FEL.begin(); it != FEL.end();) {

        //if iterator is ahead of clock time end the loop
        if (it->time > clock) {
            break; 
        }

        // if clock time is same as the FEL's arrival event time
        if (it->time == clock && it->type == ARRIVAL) {
            
            // iterator points at an event so we can take customer id from that event
            int customer_id = it->customer_id;

            cout << "  ARRIVAL:   Customer " << customer_id + 1 << " arrived at t=" << clock << "." << endl;
            log_file << "  ARRIVAL:   Customer " << customer_id + 1 << " arrived at t=" << clock << "." << endl;

            Server* best_server = nullptr;
            int min_service_time = INT_MAX;

            // pair type -> string(server name), Server(server structure)
            for (auto& pair : server_states) {
                Server& current_server = pair.second;
                
                if (current_server.status == IDLE) {
                    
                    // check server's service time for this customer
                    int potential_service_time = service_samples.at(current_server.name)[customer_id];
                    
                    // if this one is the best one yet assign it as the best server
                    if (potential_service_time < min_service_time) {

                        min_service_time = potential_service_time;
                        best_server = &current_server;
                    }
                }
            }

            if (best_server != nullptr) {
                
                
                // get the service time
                int service_time = service_samples.at(best_server->name)[customer_id];
                int finish_time = clock + service_time;

                stats.server_total_busy_time[best_server->name] += service_time;

                // set our idle server to busy and give the customer id to be serviced
                best_server->status = BUSY;
                best_server->current_customer_id = customer_id;

                // adding departure event
                Event departure_event{DEPARTURE, finish_time, customer_id, best_server->name};

                auto it_insert = it; //start from current location in FEL
                ++it_insert; // move one step
                while (it_insert != FEL.end() && it_insert->time < departure_event.time) {
                    //this while loop adjusts the iterator to it's proper position according to clock time
                    ++it_insert;
                }

                // adding departure event to FEL
                FEL.insert(it_insert, departure_event);

                cout << "    -> Assigned to server '" << best_server->name << "'."
                     << " Service time: " << service_time << " min."
                     << " (Finishes at t=" << finish_time << ")" << endl;

            } else {    //all servers are busy, add customer to waiting list

                // create new customer
                Customer new_customer;
                new_customer.id = customer_id;
                new_customer.arrival_time = clock;

                // add the customer to waiting list
                waiting_list.push(new_customer);

                if ((int)waiting_list.size() > stats.max_queue_length) {
                    stats.max_queue_length = (int)waiting_list.size();
                }

                cout << "    -> All servers busy. Customer " << customer_id + 1
                     << " added to waiting list." << endl;
            }

            //now we handled the arrival event we can delete it from the FEL
            it = FEL.erase(it);

        } else {
            //if it is not an arrival event, pass
            ++it;
        }
    }
}

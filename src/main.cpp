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


static inline string trim(const string& s) {
    size_t b = 0, e = s.size();
    while (b < e && isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && isspace(static_cast<unsigned char>(s[e-1]))) --e;
    return s.substr(b, e - b);
}

// ------- ARRIVAL (inter-arrival) tarafı (senin mevcut fonksiyonların) -------
vector<ArrivalData> readArrivalDataFromCSV(const string& filename) {
    vector<ArrivalData> raw_data;
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "ERROR: cannot open file " << filename << endl;
        exit(1);
    }
    string line;
    if (!getline(file, line)) {
        cerr << "ERROR: empty file " << filename << endl;
        exit(1);
    } // header'ı yut
    while (getline(file, line)) {
        line = trim(line);
        if (line.empty()) continue;
        stringstream ss(line);
        string seg;
        getline(ss, seg, ',');
        int t = stoi(trim(seg));
        getline(ss, seg, ',');
        double p = stod(trim(seg));
        raw_data.push_back({t, p});
    }
    return raw_data;
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
        // minik kayan nokta hataları için clamp
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

// ----------------- SERVICE tarafı: çoklu server parser + örnekleme -----------------
struct ServerSpec {
    string name;
    vector<ArrivalData> dist;          // (time, prob)
    vector<CumulativeData> cumulative; // kümülatif
};

// services.csv dosyasını okur: server adı blok başlığı, altı (time,prob) satırları
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
                servers[current] = ServerSpec{current, {}, {}};
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
void handle_departures(int clock, list<Event>& FEL, map<string, Server>& server_states);
void handle_waiting_list(int clock, queue<Customer>& waiting_list, map<string, Server>& server_states,
                         list<Event>& FEL, const unordered_map<string, vector<int>>& service_samples);
void handle_arrivals(int clock, list<Event>& FEL, queue<Customer>& waiting_list, 
                     map<string, Server>& server_states, const unordered_map<string, vector<int>>& service_samples);

// ----------------------------------- main -----------------------------------
int main() {

    //----------------------------------- Phase-1 Initialize -----------------------------------
    
    mt19937 gen(42); //her seferinde sabit randomları üretiyor debug için uygun sonra değişmesi lazım

    const string ARRIVALS_FILE = "config/arrivals.csv";
    const string SERVICES_FILE = "config/services.csv";

    
    auto raw_arrival_data = readArrivalDataFromCSV(ARRIVALS_FILE);
    auto servers = readServiceDataFromCSV(SERVICES_FILE);

    cout << "Multi-Server Queue Simulation — Bootstrap OK. Files loaded.\n\n";

    int customers;
    cout << "How many customers: ";
    cin >> customers;

    vector<int> inter_arrival_times = fillArrivalList(customers, raw_arrival_data, gen);
    auto arrival_times = createArrivalTimes(inter_arrival_times);

    // builds multiple servers from services.csv
    buildCumulativeForServers(servers);
    // create a service lists that is long as customer amount for every server
    unordered_map<string, vector<int>> service_samples;
    for (const auto& kv : servers) {
        service_samples[kv.first] = generateServiceTimesPerServer(kv.second, customers, gen);
    }


    cout << "Generated Arrival Times (t=0):" << endl;
    for(size_t i = 0; i < arrival_times.size(); ++i) {
        cout << "Cst " << i+1 << ":" << setw(3) << arrival_times[i] << (( (i+1) % 10 == 0) ? "\n" : " ");
    }
    cout << "\n\n";

    cout << "Service Times for Each Server" << endl;
    
    for (auto kv : service_samples){
        cout << "Server: " << kv.first << endl;
        cout << "---------------------------" << endl;
        int idx = 1;
        for (auto i : kv.second){
            cout << idx << " - " << i << endl;
            idx++;
        }
        cout << endl;
    }

    //----------------------------------- Phase-2 Simulation Start -----------------------------------

    cout << "\n\n--- SIMULATION STARTING ---" << endl;

    list<Event> FEL; // Future Event List (contains events to happen like customer Arrival or customer Departure)
    queue<Customer> waiting_list;   //customer's waiting line
    map<string, Server> server_states; //server's current state (IDLE or BUSY)

    for (const auto& server : servers) {
        string server_name = server.first;
        server_states[server_name] = Server{server_name, IDLE, -1};
    }

    for(int i = 0; i < arrival_times.size(); ++i){
        int customer_id = i;
        int customer_arrival_time = arrival_times[i];

        Event arrival_event{ARRIVAL, customer_arrival_time, customer_id, ""};
        FEL.push_back(arrival_event);
    }

    cout << "  [SETUP] FEL initialized with " << FEL.size() << " ARRIVAL events." << endl;
    cout << "  [SETUP] All " << server_states.size() << " servers initialized as IDLE." << endl;

    int clock = 0;

    while (!FEL.empty()) {

        if ((!FEL.empty() && FEL.front().time == clock) || !waiting_list.empty()) {
             cout << "\n--- [CLOCK: " << clock << "] ---" << endl;
        } else {
            cout << "\n--- [CLOCK: " << clock << "] ---" << endl;
            cout << "  (No events scheduled, queue is empty.)" << endl;
        }

        // -------------------------------------------------
        // 
        handle_departures(clock, FEL, server_states);
        handle_waiting_list(clock, waiting_list, server_states, FEL, service_samples);
        handle_arrivals(clock, FEL, waiting_list, server_states, service_samples);
        //
        // -------------------------------------------------


        clock++;
        

        if (clock > 99999) {
            cout << "  [WARN] Simulation limit reached (99999). Breaking loop." << endl;
            break;
        }
    }

    return 0;
}

void handle_departures(int clock, list<Event>& FEL, map<string, Server>& server_states) {
    

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
                         const unordered_map<string, vector<int>>& service_samples) 
{
    if (waiting_list.empty()) {
        return;
    }

    // pair type -> string(server name), Server(server structure)
    for (auto& pair : server_states) {
        
        Server& server = pair.second;
        
        if (server.status == IDLE) {
            //if waiting list is not empty we selected this server

            if (waiting_list.empty()) {
                break;
            }

            //getting the customer whose going to be served
            Customer customer_to_serve = waiting_list.front();
            waiting_list.pop();

            //getting the service and the finish time for our customer
            int service_time = service_samples.at(server.name)[customer_to_serve.id];
            int finish_time = clock + service_time;

            //set selected server to busy and customer being served
            server.status = BUSY;
            server.current_customer_id = customer_to_serve.id;

            //adding departure event
            Event departure_event{DEPARTURE, finish_time, customer_to_serve.id, server.name};

            //adding departure event to FEL
            auto it = FEL.begin();
            while (it != FEL.end() && it->time < departure_event.time) {    // bringing our iterator to the end of FEL
                ++it;
            }
            
            //this adds the departure event to the end of FEL
            FEL.insert(it, departure_event);

            cout << "  QUEUE->SERVER: Customer " << customer_to_serve.id + 1
                 << " (waited) assigned to server '" << server.name << "'."
                 << " Service time: " << service_time << " min."
                 << " (Finishes at t=" << finish_time << ")" << endl;
        }
    }
}

void handle_arrivals(int clock, 
                     list<Event>& FEL, 
                     queue<Customer>& waiting_list, 
                     map<string, Server>& server_states,
                     const unordered_map<string, vector<int>>& service_samples) 
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

            Server* idle_server = nullptr;

            // pair type -> string(server name), Server(server structure)
            for (auto& pair : server_states) {
                //idle server found
                if (pair.second.status == IDLE) {
                    idle_server = &pair.second; 
                    break;
                }
            }

            if (idle_server != nullptr) {
                
                
                // get the service time
                int service_time = service_samples.at(idle_server->name)[customer_id];
                int finish_time = clock + service_time;

                // set our idle server to busy and give the customer id to be serviced
                idle_server->status = BUSY;
                idle_server->current_customer_id = customer_id;

                // adding departure event
                Event departure_event{DEPARTURE, finish_time, customer_id, idle_server->name};

                auto it_insert = it; //start from current location in FEL
                ++it_insert; // move one step
                while (it_insert != FEL.end() && it_insert->time < departure_event.time) {
                    //this while loop adjusts the iterator to it's proper position according to clock time
                    ++it_insert;
                }

                // adding departure event to FEL
                FEL.insert(it_insert, departure_event);

                cout << "    -> Assigned to server '" << idle_server->name << "'."
                     << " Service time: " << service_time << " min."
                     << " (Finishes at t=" << finish_time << ")" << endl;

            } else {    //all servers are busy, add customer to waiting list

                // create new customer
                Customer new_customer;
                new_customer.id = customer_id;
                new_customer.arrival_time = clock;

                // add the customer to waiting list
                waiting_list.push(new_customer);

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

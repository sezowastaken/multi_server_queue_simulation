#include <iostream>
#include <list>
#include <vector>
#include <random>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <cctype>

using namespace std;

struct ArrivalData {
    int time;
    double prob;
};

struct CumulativeData {
    int time;
    double cumulative_prob;
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

list<int> fillArrivalList(int total_events, const vector<ArrivalData>& raw_data, mt19937& gen) {
    auto cum = calculateCumulative(raw_data);
    list<int> result;
    for (int i = 0; i < total_events; ++i) {
        result.push_back(generateInterArrivalTime(cum, gen));
    }
    return result;
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
    string current; // aktif server adı

    string line;
    bool firstLine = true;
    while (getline(file, line)) {
        line = trim(line);
        if (line.empty()) continue;

        // ilk satır header ise atla
        if (firstLine) {
            firstLine = false;
            string lower = line;
            for (auto& ch : lower) ch = tolower(static_cast<unsigned char>(ch));
            if (lower == "time,prob" || lower == "time;prob") continue;
        }

        // virgül yoksa bu bir server adı kabul edilir
        if (line.find(',') == string::npos) {
            current = line; // yeni bölüm
            if (servers.find(current) == servers.end()) {
                servers[current] = ServerSpec{current, {}, {}};
            }
            continue;
        }

        // veri satırı bekleniyor
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

    // basit doğrulama
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

// ----------------------------------- main -----------------------------------
int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    const string ARRIVALS_FILE = "config/arrivals.csv";
    const string SERVICES_FILE = "config/services.csv";

    // RNG: tek generator kullan (tekrar üretilebilirlik istersen sabit tohum ver)
    mt19937 gen(42); // veya: mt19937 gen(random_device{}());

    // inter-arrival dağılımını oku
    auto raw_arrival_data = readArrivalDataFromCSV(ARRIVALS_FILE);

    cout << "Multi-Server Queue Simulation — Bootstrap OK. Files loaded.\n\n";
    cout << "[Arrival distribution]\n";
    for (const auto& d : raw_arrival_data) {
        cout << "  t=" << d.time << "  p=" << fixed << setprecision(2) << d.prob << "\n";
    }
    cout << "------------------------------------------\n\n";

    int customers;
    cout << "How many customers: ";
    cin >> customers;

    // inter-arrival örnekle
    auto inter_arrival_times = fillArrivalList(customers, raw_arrival_data, gen);

    // services.csv → çoklu server
    auto servers = readServiceDataFromCSV(SERVICES_FILE);
    buildCumulativeForServers(servers);

    // her server için customers kadar service time üret
    unordered_map<string, vector<int>> service_samples;
    for (const auto& kv : servers) {
        service_samples[kv.first] = generateServiceTimesPerServer(kv.second, customers, gen);
    }

    // --- Örnek çıktı: inter-arrival ve server bazlı service times (10’lu sütunlar) ---
    cout << "\nGenerated Inter-Arrival Times:\n";
    int idx = 1;
    for (auto v : inter_arrival_times) {
        cout << "Cst " << idx << ":" << setw(2) << v << ((idx % 10 == 0) ? "\n" : " ");
        ++idx;
    }
    cout << "\n\n";

    for (auto& kv : service_samples) {
        cout << "Service times for server [" << kv.first << "]:\n";
        int i = 1;
        for (int x : kv.second) {
            cout << "C #" << i << ":" << setw(2) << x << " --- " << ((i % 10 == 0) ? "\n" : " ");
            ++i;
        }
        cout << "\n\n";
    }

    return 0;
}

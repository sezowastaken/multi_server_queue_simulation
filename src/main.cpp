#include <iostream>
#include <random>
#include "distributions.hpp"
using namespace std;

int main(){
    cout << "CSV+Sampling smoke test\n";
    try{
        auto arrival = DiscreteDist::fromCsv("config/arrivals.csv");
        auto service = DiscreteDist::fromCsv("config/services.csv");
        std::mt19937 gen(12345);
        cout << "Inter-arrival samples: ";
        for(int i=0;i<10;++i) cout << arrival.sample(gen) << (i<9?' ':'\n');
        cout << "Service samples:      ";
        for(int i=0;i<10;++i) cout << service.sample(gen) << (i<9?' ':'\n');
        cout << "OK\n";
    }catch(const std::exception& e){ cerr << "ERROR: " << e.what() << "\n"; return 1; }
    return 0;
}

#include "distributions.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cctype>

static std::string trim(const std::string& s){
    size_t a=0,b=s.size();
    while(a<b && std::isspace((unsigned char)s[a])) ++a;
    while(b>a && std::isspace((unsigned char)s[b-1])) --b;
    return s.substr(a,b-a);
}

DiscreteDist DiscreteDist::fromCsv(const std::string& path){
    std::ifstream in(path);
    if(!in) throw std::runtime_error("Cannot open file: " + path);

    DiscreteDist d; std::string line; bool header=false;
    while(std::getline(in,line)){
        line = trim(line); if(line.empty()) continue;
        if(!header){ header=true;
            if(line.find("time")!=std::string::npos || line.find("prob")!=std::string::npos) continue;
        }
        std::stringstream ss(line);
        std::string a,b; if(!std::getline(ss,a,',')) continue; if(!std::getline(ss,b,',')) continue;
        d.entries.push_back({std::stoi(trim(a)), std::stod(trim(b))});
    }
    if(d.entries.empty()) throw std::runtime_error("No entries in: " + path);
    d.build(); return d;
}

void DiscreteDist::build(){
    double sum=0.0; for(auto &e: entries) sum+=e.prob;
    if(sum<=0.0) throw std::runtime_error("Probabilities sum <= 0");
    for(auto &e: entries) e.prob/=sum;
    cum.clear(); cum.reserve(entries.size());
    double r=0.0; for(auto &e: entries){ r+=e.prob; if(r>1.0) r=1.0; cum.push_back(r); }
    if(!cum.empty()) cum.back()=1.0;
}

int DiscreteDist::sample(std::mt19937& gen) const{
    std::uniform_real_distribution<double> U(0.0,1.0);
    double u=U(gen);
    for(size_t i=0;i<cum.size();++i) if(u<=cum[i]) return entries[i].value;
    return entries.back().value;
}

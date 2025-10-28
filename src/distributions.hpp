#pragma once
#include <vector>
#include <string>
#include <random>

struct DistEntry { int value; double prob; };

struct DiscreteDist {
    std::vector<DistEntry> entries;
    std::vector<double> cum;
    static DiscreteDist fromCsv(const std::string& path);
    void build();
    int sample(std::mt19937& gen) const;
};

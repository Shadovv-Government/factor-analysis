#include "core.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
namespace fs = std::filesystem;
namespace {
void check(bool ok, const std::string& what) { if (!ok) throw std::runtime_error(what); }
std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    check(bool(f), "cannot read " + p.string());
    return {std::istreambuf_iterator<char>(f), {}};
}
std::uint64_t fnv1a(const std::string& s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}
void golden() {
    const auto dir = fs::temp_directory_path() / "portfolio-golden-test";
    fs::remove_all(dir);
    pg::Config c;
    pg::report(c, pg::generate(c), dir);
    const auto hash = fnv1a(slurp(dir / "portfolio.csv"));
    fs::remove_all(dir);
    check(hash == 0xadb2f8f545e20656ULL, "seed 42 portfolio changed: algorithm output is no longer reproducible");
}
bool same(const pg::Config& a, const pg::Config& b) {
    return a.count == b.count && a.seed == b.seed && a.min_amount == b.min_amount && a.max_amount == b.max_amount &&
           a.min_term == b.min_term && a.max_term == b.max_term && a.shares == b.shares && a.floating == b.floating &&
           a.min_rate == b.min_rate && a.max_rate == b.max_rate && a.min_spread == b.min_spread && a.max_spread == b.max_spread;
}
void profiles(const fs::path& dir) {
    for (const char* n : {"short", "long", "floating"})
        check(same(pg::load(dir / (std::string(n) + ".ini")), pg::profile(n)), std::string("profile differs from INI: ") + n);
}
void errors() {
    const auto dir = fs::temp_directory_path() / "portfolio-config-errors";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto expect = [&](const std::string& body, const std::string& needle) {
        const auto path = dir / "bad.ini";
        { std::ofstream f(path, std::ios::binary); f << body; }
        try { pg::load(path); }
        catch (const std::exception& e) {
            check(std::string(e.what()).find(needle) != std::string::npos, "expected '" + needle + "' in: " + e.what());
            return;
        }
        check(false, "no error for: " + body);
    };
    expect("version=1.0.0\nmin_term=abc\n", "line 2");
    expect("count=10\ncount=20\n", "duplicate key 'count'");
    expect("bogus=1\n", "unknown key 'bogus'");
    expect("seed=5000000000\n", "seed");
    expect("min_term=-3\n", "'min_term'");
    expect("count=10\n\nmax_term\n", "line 3");
    fs::remove_all(dir);
}
}
int main(int argc, char** argv) {
    try {
        check(argc >= 2, "mode required");
        const std::string mode = argv[1];
        if (mode == "golden") golden();
        else if (mode == "profiles") { check(argc == 3, "profiles dir required"); profiles(fs::u8path(argv[2])); }
        else if (mode == "errors") errors();
        else throw std::runtime_error("unknown mode");
        std::cout << mode << " ok\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

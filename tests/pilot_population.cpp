// Population pilot: one sensory population (thalamic relay + TRN) -> one cortical population
// (80% RS excitatory, 20% FS inhibitory). Inter-population projections excitatory only;
// inhibition is local. Sparse random wiring (connection probability p) with balanced-network
// scaling J/sqrt(K) (van Vreeswijk & Sompolinsky 1996; Brunel 2000).
// Runs the four acceptance checks at several population sizes and reports wall cost.
//
// usage: pilot_population [N ...]          (default 30 60 120)
//        pilot_population sweep N           (coarse J_E x g sweep at size N)
#include "metabrain/SpikingCore.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

using namespace metabrain::spiking;

struct Params {
    float p = 0.2f;        // connection probability within / between populations
    float JE = 0.6f;       // excitatory coupling: per-synapse conductance = JE / sqrt(K)
    float g = 5.0f;        // inhibitory / excitatory strength ratio (Brunel g)
    float Jff = 0.8f;      // sensory -> cortex (E and I) coupling, / sqrt(K_ff)
    float gain = 12.0f;    // sensory current per unit drive
};

struct Pop { std::vector<uint32_t> E, I; };

struct Net {
    SpikingNetwork net;
    Pop thal, ctx;
    std::vector<float> excit;  // per-cell heterogeneity
    explicit Net(SpikingConfig c) : net(c) {}
};

void Wire(Net& n, const std::vector<uint32_t>& pre, const std::vector<uint32_t>& post, float J, float p, std::mt19937& r) {
    const float K = std::max(1.0f, p * pre.size());
    const float w = J / std::sqrt(K);
    std::bernoulli_distribution b(p);
    std::uniform_real_distribution<float> d(1.0f, 3.0f);
    for (uint32_t a : pre) for (uint32_t c : post) if (a != c && b(r)) n.net.Connect(a, c, w, d(r), true);
}

Net Build(int N, const Params& P, uint32_t seed) {
    SpikingConfig c;
    c.w_max = 10.0f;  // ceiling must not clip I->E (g * JE / sqrt(K))
    Net n(c);
    const int nE = N * 4 / 5, nI = N - nE;
    for (int i = 0; i < nE; ++i) n.thal.E.push_back(n.net.AddNeuron(CellType::ThalamoCortical));
    for (int i = 0; i < nI; ++i) n.thal.I.push_back(n.net.AddNeuron(CellType::LowThresholdSpiking));  // TRN
    for (int i = 0; i < nE; ++i) n.ctx.E.push_back(n.net.AddNeuron(CellType::RegularSpiking));
    for (int i = 0; i < nI; ++i) n.ctx.I.push_back(n.net.AddNeuron(CellType::FastSpiking));
    std::mt19937 r(seed);
    for (Pop* pop : {&n.thal, &n.ctx}) {
        Wire(n, pop->E, pop->E, P.JE * (pop == &n.thal ? 0.0f : 1.0f), P.p, r);  // relay cells don't interconnect
        Wire(n, pop->E, pop->I, P.JE, P.p, r);
        Wire(n, pop->I, pop->E, P.JE * P.g, P.p, r);
        Wire(n, pop->I, pop->I, P.JE * P.g, P.p, r);
    }
    // Inter-population: excitatory only, onto both E (drive) and I (feedforward inhibition)
    Wire(n, n.thal.E, n.ctx.E, P.Jff, P.p, r);
    Wire(n, n.thal.E, n.ctx.I, P.Jff, P.p, r);
    std::normal_distribution<float> h(0.0f, 1.2f);
    n.excit.resize(n.net.Size());
    for (float& x : n.excit) x = h(r);
    return n;
}

struct Rec { std::vector<std::vector<int>> st; double wall_ms; };

Rec Run(Net& n, const Params& P, int ms, float drive, uint32_t seed) {
    std::mt19937 r(seed);
    std::normal_distribution<float> noise(0.0f, 1.0f);
    const float sd = 3.0f;  // same background as the engine bridge at curiosity 0.5
    Rec rec;
    rec.st.resize(n.net.Size());
    const int sub = 10;
    const auto t0 = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < ms; ++t) {
        for (uint32_t i = 0; i < n.net.Size(); ++i) {
            float I = n.excit[i] + sd * noise(r);
            if (std::find(n.thal.E.begin(), n.thal.E.end(), i) != n.thal.E.end()) I += P.gain * drive - 1.0f;
            n.net.SetInputCurrent(i, I);
        }
        for (int s = 0; s < sub; ++s)
            for (uint32_t f : n.net.Step()) rec.st[f].push_back(t);
    }
    n.net.ClearSpikeLog();
    rec.wall_ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
    return rec;
}

struct Metrics { double rest_med, cv_med, chi; double c3, c6, c9, max9; double wall_per_bio_s; size_t syn; bool ok[4]; };

double Median(std::vector<double> v) { if (v.empty()) return 0; std::sort(v.begin(), v.end()); return v[v.size() / 2]; }

Metrics Evaluate(int N, const Params& P, uint32_t seed) {
    Metrics m{};
    {   // rest: 20 s
        Net n = Build(N, P, seed);
        Run(n, P, 500, 0.0f, seed + 1);
        const int ms = 20000;
        const Rec rec = Run(n, P, ms, 0.0f, seed + 2);
        m.wall_per_bio_s = rec.wall_ms / (ms / 1000.0);
        m.syn = 0;
        for (uint32_t i = 0; i < n.net.Size(); ++i) m.syn += n.net.OutDegree(i);
        std::vector<double> rates, cvs;
        std::vector<uint32_t> ctx = n.ctx.E;
        ctx.insert(ctx.end(), n.ctx.I.begin(), n.ctx.I.end());
        for (uint32_t i : ctx) {
            const auto& s = rec.st[i];
            if (std::find(n.ctx.E.begin(), n.ctx.E.end(), i) != n.ctx.E.end()) rates.push_back(s.size() / (ms / 1000.0));  // excitatory rest rate
            if (s.size() >= 10) {
                double mu = 0, v = 0;
                for (size_t k = 1; k < s.size(); ++k) mu += s[k] - s[k - 1];
                mu /= (s.size() - 1);
                for (size_t k = 1; k < s.size(); ++k) v += std::pow(s[k] - s[k - 1] - mu, 2);
                cvs.push_back(std::sqrt(v / (s.size() - 1)) / mu);
            }
        }
        m.rest_med = Median(rates);
        m.cv_med = Median(cvs);
        const int bins = ms / 10;
        std::vector<double> pop(bins, 0.0);
        double mv = 0;
        int nc = 0;
        for (uint32_t i : ctx) {
            if (rec.st[i].empty()) continue;
            std::vector<double> x(bins, 0.0);
            for (int t : rec.st[i]) x[t / 10] += 1;
            double mu = 0, v = 0;
            for (double b : x) mu += b;
            mu /= bins;
            for (int b = 0; b < bins; ++b) { v += (x[b] - mu) * (x[b] - mu); pop[b] += x[b]; }
            mv += v / bins;
            ++nc;
        }
        double pm = 0, pv = 0;
        for (double& b : pop) { b /= std::max(1, nc); pm += b; }
        pm /= bins;
        for (double b : pop) pv += (b - pm) * (b - pm);
        m.chi = nc ? std::sqrt((pv / bins) / (mv / nc)) : 1.0;
    }
    double* out[3] = {&m.c3, &m.c6, &m.c9};
    const float drives[3] = {0.3f, 0.6f, 0.9f};
    for (int k = 0; k < 3; ++k) {
        Net n = Build(N, P, seed);
        Run(n, P, 500, 0.0f, seed + 1);
        const Rec rec = Run(n, P, 2000, drives[k], seed + 3);
        double s = 0, mx = 0;
        for (uint32_t i : n.ctx.E) s += rec.st[i].size() / 2.0;
        for (const Pop* pp : {&n.thal, &n.ctx}) for (uint32_t i : pp->E) mx = std::max(mx, rec.st[i].size() / 2.0);  // excitatory cells; PV may reach 200-400 Hz
        *out[k] = s / n.ctx.E.size();
        if (k == 2) m.max9 = mx;
    }
    m.ok[0] = m.rest_med >= 0.5 && m.rest_med <= 3.0;
    m.ok[1] = m.cv_med >= 0.8 && m.cv_med <= 1.2;
    m.ok[2] = m.chi < 0.35;
    m.ok[3] = m.c3 < m.c6 && m.c6 < m.c9 && m.max9 < 150;
    return m;
}

void Print(int N, const Params& P, const Metrics& m) {
    std::printf("N=%4d JE=%.2f g=%.1f Jff=%.2f | rest %5.2f Hz %s | CV %4.2f %s | chi %.3f %s | ctx E @.3/.6/.9: %5.2f %5.2f %5.2f max %3.0f %s | %6zu syn, %6.1f ms/bio-s | %s\n",
                N, P.JE, P.g, P.Jff, m.rest_med, m.ok[0] ? "ok" : "--", m.cv_med, m.ok[1] ? "ok" : "--", m.chi, m.ok[2] ? "ok" : "--",
                m.c3, m.c6, m.c9, m.max9, m.ok[3] ? "ok" : "--", m.syn, m.wall_per_bio_s,
                (m.ok[0] && m.ok[1] && m.ok[2] && m.ok[3]) ? "PASS" : "fail");
}

int main(int argc, char** argv) {
    Params P;
    if (argc > 2 && std::strcmp(argv[1], "sweep") == 0) {
        const int N = std::atoi(argv[2]);
        for (float JE : {0.3f, 0.6f, 1.0f})
            for (float g : {4.0f, 6.0f, 8.0f})
                for (float Jff : {0.2f, 0.4f, 0.8f}) {
                    Params q = P; q.JE = JE; q.g = g; q.Jff = Jff;
                    Print(N, q, Evaluate(N, q, 42));
                }
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "p") == 0) {  // pilot_population p JE g Jff N...
        P.JE = std::atof(argv[2]); P.g = std::atof(argv[3]); P.Jff = std::atof(argv[4]);
        for (int k = 5; k < argc; ++k) for (uint32_t s : {42u, 7u, 1234u}) Print(std::atoi(argv[k]), P, Evaluate(std::atoi(argv[k]), P, s));
        return 0;
    }
    std::vector<int> Ns;
    for (int k = 1; k < argc; ++k) Ns.push_back(std::atoi(argv[k]));
    if (Ns.empty()) Ns = {30, 60, 120};
    for (int N : Ns) Print(N, P, Evaluate(N, P, 42));
    return 0;
}

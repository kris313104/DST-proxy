// test_parser.cpp — testy jednostkowe parsera (bez zewnętrznych zależności).
#include "prox_parser.h"

#include <cmath>
#include <iostream>
#include <string>

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::cerr << "FAIL @" << __LINE__ << ": " << #cond << "\n";   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

int main() {
    using namespace prox;

    // --- Linia [PROX] z prefiksem czasu DST ---
    {
        std::string line =
            "[00:01:23]: [PROX] v=1 t=123.45 uid=\"KU_abcd1234\" name=\"Wilson\" "
            "x=12.34 z=-56.78 h=45.0 room=\"sess_xyz\"";
        Update u;
        Ids ids;
        CHECK(parse_line(line, u, ids) == LineKind::Update);
        CHECK(u.version == 1);
        CHECK(near(u.t, 123.45));
        CHECK(u.uid == "KU_abcd1234");
        CHECK(u.name == "Wilson");
        CHECK(near(u.x, 12.34));
        CHECK(near(u.z, -56.78));
        CHECK(near(u.heading, 45.0));
        CHECK(u.room == "sess_xyz");
    }

    // --- Linia [PROX-IDS] nie może być potraktowana jako Update ---
    {
        std::string line =
            "[00:01:24]: [PROX-IDS] sid=\"sess_xyz\" sid2=\"\" srv=\"My Server\" "
            "chosen_room=\"sess_xyz\"";
        Update u;
        Ids ids;
        CHECK(parse_line(line, u, ids) == LineKind::Ids);
        CHECK(ids.sid == "sess_xyz");
        CHECK(ids.sid2.empty());
        CHECK(ids.srv == "My Server");  // spacja wewnątrz cudzysłowu zachowana
        CHECK(ids.chosen_room == "sess_xyz");
    }

    // --- Escapowany cudzysłów w nazwie ---
    {
        std::string line =
            "[PROX] v=1 t=0.0 uid=\"KU_x\" name=\"Bob \\\"the\\\" Builder\" "
            "x=0 z=0 h=0 room=\"r\"";
        Update u;
        Ids ids;
        CHECK(parse_line(line, u, ids) == LineKind::Update);
        CHECK(u.name == "Bob \"the\" Builder");
    }

    // --- Linia niezwiązana ---
    {
        std::string line = "[00:00:01]: Loading mod: workshop-12345";
        Update u;
        Ids ids;
        CHECK(parse_line(line, u, ids) == LineKind::None);
    }

    // --- parse_kv: liczby i wartości puste ---
    {
        auto kv = parse_kv(" a=1 b=\"two words\" c= d=-3.5 ");
        CHECK(kv["a"] == "1");
        CHECK(kv["b"] == "two words");
        CHECK(kv["c"].empty());
        CHECK(kv["d"] == "-3.5");
    }

    if (g_failures == 0) {
        std::cout << "OK: wszystkie testy parsera przeszly\n";
        return 0;
    }
    std::cerr << g_failures << " test(ow) nie przeszlo\n";
    return 1;
}

// hms_seed - developer tool: fills a data store with synthetic patients and
// optionally times patient searches (groundwork for TC-Perf-01, HMS-NF-001).
//
// Usage: hms_seed --store PATH --count N [--bench] [--key PASSPHRASE | --plain]
//
// Test data only. Records are inserted directly through the repository in
// one transaction, without audit entries, and must never be used on a store
// that holds real patient data.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "appointment/appointment_service.hpp"
#include "auth/authentication.hpp"
#include "config/policy.hpp"
#include "core/clock.hpp"
#include "data/store.hpp"
#include "patient/patient_repository.hpp"
#include "patient/patient_service.hpp"
#include "platform/store_lock.hpp"

using namespace hms;

namespace {
const std::vector<std::string> kFirst = {"Aarav",  "Vivaan", "Aditya", "Vihaan", "Arjun",  "Sai",    "Reyansh",
                                         "Krishna", "Ishaan", "Rohan",  "Ananya", "Diya",   "Saanvi", "Aadhya",
                                         "Kavya",  "Priya",  "Lakshmi", "Meera",  "Fatima", "Joseph", "Harpreet",
                                         "Suresh", "Ramesh", "Geeta",  "Sunita", "Mohammed", "Thomas", "Venkat"};
const std::vector<std::string> kLast = {"Sharma", "Verma", "Reddy",  "Rao",    "Nair",   "Iyer",  "Menon",
                                        "Gupta",  "Singh", "Kumar",  "Patel",  "Shah",   "Khan",  "Das",
                                        "Bose",   "Pillai", "Naidu", "Joshi",  "Mehta",  "Chopra", "Banerjee",
                                        "Mukherjee", "Fernandes", "DSouza", "Hegde", "Gowda", "Shetty", "Kamath"};
const std::vector<std::string> kAreas = {"Jayanagar", "Koramangala", "Indiranagar", "Whitefield", "Malleshwaram",
                                         "Banashankari", "HSR Layout", "Electronic City", "Hebbal", "Yelahanka"};

double percentile(std::vector<double> v, double p) {
    std::sort(v.begin(), v.end());
    const auto idx = static_cast<std::size_t>(p * static_cast<double>(v.size() - 1));
    return v[idx];
}
}  // namespace

int main(int argc, char** argv) {
    std::string store;
    long long count = 0;
    bool bench = false;
    // Test stores are encrypted like real ones, so the timings include the cipher (HMS-NF-003).
    std::string key = "hms-bench-passphrase";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--store" && i + 1 < argc) store = argv[++i];
        else if (a == "--count" && i + 1 < argc) count = std::stoll(argv[++i]);
        else if (a == "--bench") bench = true;
        else if (a == "--key" && i + 1 < argc) key = argv[++i];
        else if (a == "--plain") key.clear();  // an unencrypted store, for comparing timings only
        else {
            std::cerr << "Usage: hms_seed --store PATH --count N [--bench] [--key PASSPHRASE | --plain]\n";
            return 2;
        }
    }
    if (store.empty()) {
        std::cerr << "Usage: hms_seed --store PATH --count N [--bench] [--key PASSPHRASE | --plain]\n";
        return 2;
    }

    const std::string canonical = canonicalStorePath(store);
    std::string err;
    auto lock = StoreLock::acquire(canonical, err);
    if (!lock) {
        std::cerr << "Cannot lock store: " << err << "\n";
        return 1;
    }
    auto opened = openStore(canonical, true, key);
    if (opened.status != StoreStatus::Opened) {
        std::cerr << opened.message << "\n";
        return 1;
    }
    Database& db = *opened.db;
    PatientRepository repo(db);

    std::mt19937_64 rng(20261025);
    auto pick = [&](const std::vector<std::string>& v) { return v[rng() % v.size()]; };

    if (count > 0) {
        const auto t0 = std::chrono::steady_clock::now();
        Transaction tx(db);
        for (long long i = 0; i < count; ++i) {
            NewPatient p;
            p.fullName = pick(kFirst) + " " + pick(kLast);
            p.normalisedName = p.fullName;
            for (auto& c : p.normalisedName)
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            p.dateOfBirth = Date{1930 + static_cast<int>(rng() % 95), 1 + static_cast<int>(rng() % 12),
                                 1 + static_cast<int>(rng() % 28)};
            p.gender = (rng() % 2) ? Gender::Male : Gender::Female;
            p.address = std::to_string(1 + rng() % 400) + ", " + pick(kAreas) + ", Bengaluru";
            // About 1 in 10 synthetic patients has "no phone available" confirmed.
            if (rng() % 10 != 0) p.phone = std::to_string(6 + rng() % 4) + std::to_string(100000000 + rng() % 900000000);
            else p.noPhoneConfirmed = true;
            repo.insert(p);
        }
        tx.commit();
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::cout << "Inserted " << count << " patients in " << ms << " ms. Store now holds " << repo.count()
                  << " patients.\n";
    }

    if (bench) {
        SystemClock clock;
        PatientService svc(db, clock, AppPolicy{});
        Session s{"BENCH", "bench", Role::Receptionist, true};
        // HMS-NF-001: 90 % of operations within 3 seconds, so the 90th percentile is what counts.
        constexpr double kLimitMs = 3000.0;
        auto report = [&](const char* label, const std::vector<double>& times) {
            const double p90 = percentile(times, 0.90);
            std::cout << label << ": p50 " << percentile(times, 0.50) << " ms, p90 " << p90 << " ms, p95 "
                      << percentile(times, 0.95) << " ms, max " << *std::max_element(times.begin(), times.end())
                      << " ms (" << times.size() << " runs; HMS-NF-001 p90 <= " << kLimitMs << " ms: "
                      << (p90 <= kLimitMs ? "PASS" : "FAIL") << ")\n";
        };
        auto run = [&](const char* label, SearchMode mode, const std::vector<std::string>& queries) {
            std::vector<double> times;
            for (int i = 0; i < 100; ++i) {
                const auto t0 = std::chrono::steady_clock::now();
                auto r = svc.searchPatients(s, mode, queries[static_cast<std::size_t>(i) % queries.size()]);
                const auto t1 = std::chrono::steady_clock::now();
                if (!r) {
                    std::cerr << "search failed: " << r.error().describe() << "\n";
                    return;
                }
                times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            }
            report(label, times);
        };
        run("Search by UHID ", SearchMode::Uhid, {"1", "500", "HMS-000777", "42"});
        run("Search by name ", SearchMode::Name, {"sharma", "priya", "kumar", "venkat rao", "a"});
        run("Search by phone", SearchMode::Phone, {"9876543210", "+91 81234 56789", "070000 00000"});

        // The booking screens: the list of doctors with free slots, then booking a slot (TC-Perf-01).
        AppPolicy policy;
        AppointmentService appts(db, clock, policy);
        Session admin{"BENCH-ADMIN", "bench admin", Role::Admin, true};
        std::int64_t doctorId = 0;
        {
            auth::createStaffAccount(db, "benchdoc", "SecurePass123!", auth::Role::Doctor, clock, "BENCH");
            auto st = db.prepare("SELECT staff_id FROM staff_accounts WHERE username = 'benchdoc'");
            if (st.step()) doctorId = st.getInt64(0);
        }
        DoctorScheduleDraft hours;
        hours.department = "General Medicine";
        for (int day = 1; day <= 7; ++day) hours.hours.push_back(WeeklyHours{day, 9 * 60, 17 * 60});
        if (doctorId == 0 || !appts.setDoctorSchedule(admin, doctorId, hours)) {
            std::cerr << "could not set up the benchmark doctor\n";
            return 1;
        }
        const auto patientCount = static_cast<std::int64_t>(repo.count());
        if (patientCount < 1) {
            std::cerr << "the booking benchmark needs patients: use --count N\n";
            return 1;
        }
        std::vector<double> screen, book;
        for (int i = 0; i < 100; ++i) {
            // Tomorrow onward, so every slot is in the future whatever time the benchmark runs.
            const Date t = appts.today();
            const Date day = civilFromDays(daysFromCivil(t.year, t.month, t.day) + 1 + i / 30);

            auto t0 = std::chrono::steady_clock::now();
            auto doctors = appts.doctorsOn(s, day);
            auto t1 = std::chrono::steady_clock::now();
            if (!doctors) {
                std::cerr << "doctor list failed: " << doctors.error().describe() << "\n";
                return 1;
            }
            screen.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());

            t0 = std::chrono::steady_clock::now();
            auto slots = appts.freeSlots(s, doctorId, day);
            if (!slots || slots.value().empty()) {
                std::cerr << "no free slot to book on " << day.toDisplay() << "\n";
                return 1;
            }
            const auto uhid = 1 + static_cast<std::int64_t>(rng() % static_cast<unsigned long long>(patientCount));
            auto booked = appts.book(s, uhid, doctorId, slots.value().front());
            t1 = std::chrono::steady_clock::now();
            if (!booked) {
                std::cerr << "booking failed: " << booked.error().describe() << "\n";
                return 1;
            }
            book.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        report("Booking screen (doctors and free slots)", screen);
        report("Book a slot (find free slot, then book) ", book);
        std::cout << "Note: timings exclude console display and typing.\n";
    }
    return 0;
}

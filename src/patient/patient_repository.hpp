#pragma once
// Patient persistence (Data Access Module, patient part). SQL lives here and
// nowhere else; no business rule is decided here.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "data/database.hpp"
#include "patient/patient.hpp"

namespace hms {

class PatientRepository {
public:
    explicit PatientRepository(Database& db) : db_(db) {}

    std::int64_t insert(const NewPatient& p);

    std::optional<PatientRecord> findByUhid(std::int64_t uhid);

    // Strong candidates: same normalised name, date of birth and gender.
    std::vector<PatientRecord> findStrong(const std::string& normalisedName, const Date& dob, Gender gender,
                                          std::optional<std::int64_t> excludeUhid);

    std::vector<PatientRecord> findByPhone(const std::string& canonicalPhone,
                                           std::optional<std::int64_t> excludeUhid);

    // Case-insensitive substring search on the normalised name.
    std::size_t countByNameContains(const std::string& normalisedQuery);
    std::vector<PatientRecord> findByNameContains(const std::string& normalisedQuery, std::size_t limit);

    // Name contains the query and the date of birth matches (estimated dates of
    // birth match within one year). Exact matches are listed first.
    std::size_t countByNameAndDob(const std::string& normalisedQuery, const Date& dob);
    std::vector<PatientRecord> findByNameAndDob(const std::string& normalisedQuery, const Date& dob,
                                                std::size_t limit);

    std::size_t count();

    // Duplicate-review decision with its reviewed set (HMS-F-002(4),(9)).
    void insertDuplicateReview(const std::string& eventId, const std::string& decision,
                               std::optional<std::int64_t> chosenUhid, std::optional<std::int64_t> createdUhid,
                               const std::vector<Candidate>& candidates);

private:
    Database& db_;
};

}  // namespace hms

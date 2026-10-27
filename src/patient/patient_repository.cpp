#include "patient/patient_repository.hpp"

namespace hms {

namespace {
const char* kColumns = "uhid, full_name, date_of_birth, dob_estimated, gender, address, phone, no_phone_confirmed";

PatientRecord readRow(const Statement& st) {
    PatientRecord r;
    r.uhid = st.getInt64(0);
    r.fullName = st.getText(1);
    r.dateOfBirth = Date::fromIso(st.getText(2)).value_or(Date{});
    r.dobEstimated = st.getInt64(3) != 0;
    r.gender = genderFromStorage(st.getText(4)).value_or(Gender::Other);
    r.address = st.getText(5);
    r.phone = st.getOptionalText(6);
    r.noPhoneConfirmed = st.getInt64(7) != 0;
    return r;
}

std::vector<PatientRecord> readAll(Statement& st) {
    std::vector<PatientRecord> out;
    while (st.step()) out.push_back(readRow(st));
    return out;
}
}  // namespace

std::int64_t PatientRepository::insert(const NewPatient& p) {
    Statement st = db_.prepare(
        "INSERT INTO patient (full_name, normalised_name, date_of_birth, dob_estimated, gender, address, phone, "
        "no_phone_confirmed) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
    st.bind(1, p.fullName)
        .bind(2, p.normalisedName)
        .bind(3, p.dateOfBirth.toIso())
        .bind(4, p.dobEstimated ? 1 : 0)
        .bind(5, to_string(p.gender))
        .bind(6, p.address)
        .bind(7, p.phone)
        .bind(8, p.noPhoneConfirmed ? 1 : 0);
    st.run();
    return db_.lastInsertRowId();
}

std::optional<PatientRecord> PatientRepository::findByUhid(std::int64_t uhid) {
    Statement st = db_.prepare(std::string("SELECT ") + kColumns + " FROM patient WHERE uhid = ?1");
    st.bind(1, uhid);
    if (!st.step()) return std::nullopt;
    return readRow(st);
}

std::vector<PatientRecord> PatientRepository::findStrong(const std::string& normalisedName, const Date& dob,
                                                         Gender gender, std::optional<std::int64_t> excludeUhid) {
    Statement st = db_.prepare(std::string("SELECT ") + kColumns +
                               " FROM patient WHERE normalised_name = ?1 AND date_of_birth = ?2 AND gender = ?3"
                               " AND (?4 IS NULL OR uhid <> ?4) ORDER BY uhid");
    st.bind(1, normalisedName).bind(2, dob.toIso()).bind(3, to_string(gender)).bind(4, excludeUhid);
    return readAll(st);
}

std::vector<PatientRecord> PatientRepository::findByPhone(const std::string& canonicalPhone,
                                                          std::optional<std::int64_t> excludeUhid) {
    Statement st = db_.prepare(std::string("SELECT ") + kColumns +
                               " FROM patient WHERE phone = ?1 AND (?2 IS NULL OR uhid <> ?2) ORDER BY uhid");
    st.bind(1, canonicalPhone).bind(2, excludeUhid);
    return readAll(st);
}

std::size_t PatientRepository::countByNameContains(const std::string& normalisedQuery) {
    // instr() is a plain substring test: no LIKE wildcards to escape.
    Statement st = db_.prepare("SELECT count(*) FROM patient WHERE instr(normalised_name, ?1) > 0");
    st.bind(1, normalisedQuery);
    st.step();
    return static_cast<std::size_t>(st.getInt64(0));
}

std::vector<PatientRecord> PatientRepository::findByNameContains(const std::string& normalisedQuery,
                                                                 std::size_t limit) {
    Statement st = db_.prepare(std::string("SELECT ") + kColumns +
                               " FROM patient WHERE instr(normalised_name, ?1) > 0"
                               " ORDER BY normalised_name, uhid LIMIT ?2");
    st.bind(1, normalisedQuery).bind(2, static_cast<std::int64_t>(limit));
    return readAll(st);
}

namespace {
// Name contains ?1, and the date of birth is ?2 exactly, or the record's date of
// birth was estimated from an age and its year is between ?3 and ?4.
const char* kNameDobWhere =
    " FROM patient WHERE instr(normalised_name, ?1) > 0"
    " AND (date_of_birth = ?2 OR (dob_estimated = 1 AND"
    " CAST(substr(date_of_birth, 1, 4) AS INTEGER) BETWEEN ?3 AND ?4))";
}  // namespace

std::size_t PatientRepository::countByNameAndDob(const std::string& normalisedQuery, const Date& dob) {
    Statement st = db_.prepare(std::string("SELECT count(*)") + kNameDobWhere);
    st.bind(1, normalisedQuery).bind(2, dob.toIso()).bind(3, dob.year - 1).bind(4, dob.year + 1);
    st.step();
    return static_cast<std::size_t>(st.getInt64(0));
}

std::vector<PatientRecord> PatientRepository::findByNameAndDob(const std::string& normalisedQuery, const Date& dob,
                                                               std::size_t limit) {
    // Exact date-of-birth matches first, then estimated ones.
    Statement st = db_.prepare(std::string("SELECT ") + kColumns + kNameDobWhere +
                               " ORDER BY date_of_birth <> ?2, normalised_name, uhid LIMIT ?5");
    st.bind(1, normalisedQuery)
        .bind(2, dob.toIso())
        .bind(3, dob.year - 1)
        .bind(4, dob.year + 1)
        .bind(5, static_cast<std::int64_t>(limit));
    return readAll(st);
}

std::size_t PatientRepository::count() {
    return static_cast<std::size_t>(db_.queryInt("SELECT count(*) FROM patient").value_or(0));
}

void PatientRepository::insertDuplicateReview(const std::string& eventId, const std::string& decision,
                                              std::optional<std::int64_t> chosenUhid,
                                              std::optional<std::int64_t> createdUhid,
                                              const std::vector<Candidate>& candidates) {
    Statement review = db_.prepare(
        "INSERT INTO duplicate_review (event_id, decision, chosen_uhid, created_uhid) VALUES (?1, ?2, ?3, ?4)");
    review.bind(1, eventId).bind(2, decision).bind(3, chosenUhid).bind(4, createdUhid);
    review.run();

    Statement cand = db_.prepare(
        "INSERT INTO duplicate_review_candidate (event_id, uhid, kind) VALUES (?1, ?2, ?3)");
    for (const auto& c : candidates) {
        cand.bind(1, eventId).bind(2, c.patient.uhid).bind(3, to_string(c.kind));
        cand.run();
        cand.reset();
    }
}

}  // namespace hms

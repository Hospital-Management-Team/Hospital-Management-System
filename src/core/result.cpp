#include "core/result.hpp"

namespace hms {

std::string to_string(ErrorKind kind) {
    switch (kind) {
        case ErrorKind::Validation: return "Validation";
        case ErrorKind::AccessDenied: return "AccessDenied";
        case ErrorKind::NotFound: return "NotFound";
        case ErrorKind::Conflict: return "Conflict";
        case ErrorKind::StorageFailure: return "StorageFailure";
        case ErrorKind::OutcomeUnknown: return "OutcomeUnknown";
    }
    return "Unknown";
}

Error Error::validation(std::vector<FieldError> fields) {
    Error e;
    e.kind = ErrorKind::Validation;
    e.message = fields.size() == 1 ? "One field is invalid." : "Some fields are invalid.";
    e.fields = std::move(fields);
    return e;
}

Error Error::validation(std::string field, std::string message) {
    return validation(std::vector<FieldError>{FieldError{std::move(field), std::move(message)}});
}

Error Error::accessDenied(std::string message) { return Error{ErrorKind::AccessDenied, std::move(message), {}}; }
Error Error::notFound(std::string message) { return Error{ErrorKind::NotFound, std::move(message), {}}; }
Error Error::conflict(std::string message) { return Error{ErrorKind::Conflict, std::move(message), {}}; }
Error Error::storageFailure(std::string message) { return Error{ErrorKind::StorageFailure, std::move(message), {}}; }
Error Error::outcomeUnknown(std::string message) { return Error{ErrorKind::OutcomeUnknown, std::move(message), {}}; }

std::string Error::describe() const {
    if (fields.empty()) return message;
    std::string out;
    for (const auto& f : fields) {
        if (!out.empty()) out += "\n";
        out += f.field + ": " + f.message;
    }
    return out;
}

}  // namespace hms

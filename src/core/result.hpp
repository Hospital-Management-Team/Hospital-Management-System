#pragma once
// Common result type for every module function (SAD v1.10 section 4.4,
// "common contract"). A function either succeeds with a value or fails with
// exactly one Error. Business outcomes that are not failures (for example
// "possible duplicates found, staff must decide") are part of the success
// value, not errors.

#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace hms {

enum class ErrorKind {
    Validation,      // input broke a rule; each broken field is listed
    AccessDenied,    // the session's role may not perform this operation
    NotFound,        // the record asked for does not exist
    Conflict,        // a business rule refuses the operation
    StorageFailure,  // the data store failed; the change was rolled back
    OutcomeUnknown   // the commit result is not known (SRS 2.5, outcome 3)
};

std::string to_string(ErrorKind kind);

struct FieldError {
    std::string field;    // user-facing field name, e.g. "Mobile number"
    std::string message;  // the rule that was broken
};

struct Error {
    ErrorKind kind = ErrorKind::Validation;
    std::string message;
    std::vector<FieldError> fields;

    static Error validation(std::vector<FieldError> fields);
    static Error validation(std::string field, std::string message);
    static Error accessDenied(std::string message);
    static Error notFound(std::string message);
    static Error conflict(std::string message);
    static Error storageFailure(std::string message);
    static Error outcomeUnknown(std::string message);

    // One readable line per problem, for the console and for logs.
    std::string describe() const;
};

struct Unit {};  // value type for operations that return nothing

template <typename T>
class [[nodiscard]] Result {
public:
    Result(T value) : data_(std::move(value)) {}          // NOLINT(google-explicit-constructor)
    Result(Error error) : data_(std::move(error)) {}      // NOLINT(google-explicit-constructor)

    bool ok() const { return std::holds_alternative<T>(data_); }
    explicit operator bool() const { return ok(); }

    const T& value() const& { return std::get<T>(data_); }
    T& value() & { return std::get<T>(data_); }
    T&& value() && { return std::get<T>(std::move(data_)); }

    const Error& error() const { return std::get<Error>(data_); }

private:
    std::variant<T, Error> data_;
};

}  // namespace hms

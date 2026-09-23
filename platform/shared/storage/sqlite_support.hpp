#pragma once

#include <sqlite3.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace studio_remote::storage
{
inline void checked(int result)
{
  if (result != SQLITE_OK && result != SQLITE_ROW && result != SQLITE_DONE)
    throw std::runtime_error("SQLite storage operation failed");
}

inline void execute(sqlite3* database, const char* sql)
{
  checked(sqlite3_exec(database, sql, nullptr, nullptr, nullptr));
}

class Statement
{
public:
  Statement(sqlite3* database, const char* sql)
  {
    sqlite3_stmt* statement = nullptr;
    const auto result = sqlite3_prepare_v2(database, sql, -1, &statement, nullptr);
    statement_.reset(statement);
    checked(result);
  }

  void bind(int index, std::string_view value)
  {
    checked(sqlite3_bind_text(
        statement_.get(), index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT));
  }

  void bind(int index, std::uint64_t value)
  {
    checked(sqlite3_bind_int64(statement_.get(), index, static_cast<sqlite3_int64>(value)));
  }

  void bind_signed(int index, std::int64_t value)
  {
    checked(sqlite3_bind_int64(statement_.get(), index, value));
  }

  void bind_real(int index, double value)
  {
    if (!std::isfinite(value))
      throw std::runtime_error("Invalid SQLite storage value");
    checked(sqlite3_bind_double(statement_.get(), index, value));
  }

  void bind(int index, const std::optional<std::uint64_t>& value)
  {
    if (value)
      bind(index, *value);
    else
      checked(sqlite3_bind_null(statement_.get(), index));
  }

  void bind(int index, const std::optional<std::string>& value)
  {
    if (value)
      bind(index, std::string_view(*value));
    else
      checked(sqlite3_bind_null(statement_.get(), index));
  }

  bool row()
  {
    const auto result = sqlite3_step(statement_.get());
    checked(result);
    return result == SQLITE_ROW;
  }

  void finish()
  {
    if (row())
      throw std::runtime_error("Unexpected SQLite storage result");
  }

  std::string text(int column) const
  {
    const auto* value = sqlite3_column_text(statement_.get(), column);
    if (!value)
      return {};
    return {reinterpret_cast<const char*>(value),
            static_cast<std::size_t>(sqlite3_column_bytes(statement_.get(), column))};
  }

  std::uint64_t number(int column) const
  {
    const auto value = sqlite3_column_int64(statement_.get(), column);
    if (value < 0)
      throw std::runtime_error("Invalid SQLite storage value");
    return static_cast<std::uint64_t>(value);
  }

  std::int64_t signed_number(int column) const
  {
    if (sqlite3_column_type(statement_.get(), column) != SQLITE_INTEGER)
      throw std::runtime_error("Invalid SQLite storage value");
    return sqlite3_column_int64(statement_.get(), column);
  }

  double real(int column) const
  {
    const auto type = sqlite3_column_type(statement_.get(), column);
    const auto value = sqlite3_column_double(statement_.get(), column);
    if ((type != SQLITE_FLOAT && type != SQLITE_INTEGER) || !std::isfinite(value))
      throw std::runtime_error("Invalid SQLite storage value");
    return value;
  }

  std::optional<std::uint64_t> optional_number(int column) const
  {
    if (sqlite3_column_type(statement_.get(), column) == SQLITE_NULL)
      return std::nullopt;
    return number(column);
  }

private:
  struct Finalizer
  {
    void operator()(sqlite3_stmt* statement) const noexcept
    {
      // The last step error is handled before finalization, which always releases the statement.
      static_cast<void>(sqlite3_finalize(statement));
    }
  };
  std::unique_ptr<sqlite3_stmt, Finalizer> statement_;
};

class Transaction
{
public:
  explicit Transaction(sqlite3* database) : database_(database)
  {
    execute(database_, "BEGIN IMMEDIATE");
  }
  ~Transaction()
  {
    if (!committed_)
    {
      // No partial transaction may survive an error. Rollback failure leaves this connection
      // unusable and is handled by SQLite on close; it cannot turn a failed request into success.
      if (sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr) != SQLITE_OK)
        std::fputs("SQLite transaction rollback failed; restart Companion storage\n", stderr);
    }
  }
  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;
  void commit()
  {
    execute(database_, "COMMIT");
    committed_ = true;
  }

private:
  sqlite3* database_;
  bool committed_ = false;
};
} // namespace studio_remote::storage

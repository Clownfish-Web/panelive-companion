#pragma once

#include <iosfwd>
#include <string_view>

namespace studio_remote
{
/// Writes and flushes serialized configuration through a borrowed stream. The native file owner
/// must also check close before publishing its temporary file. Errors never include contents.
void write_configuration_stream(std::ostream& stream, std::string_view contents);
} // namespace studio_remote

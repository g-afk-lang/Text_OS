#include "types.h"
#include "stdlib_hooks.h"

// Implementation of the StringRef comparison operator
bool StringRef::operator==(const StringRef& other) const {
    return string_compare(data, other.data);
}

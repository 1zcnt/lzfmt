# Plans

### 0. `[implement asap]` _hooks_

Hooks should be applied as patches to member
functions. The current implementation is limited
to one hook per node.

### 1. Member list

Traverse the member tree and generate a list
of the required members, paired with their formatters,
before running them.

### 2. Compile-time evaluation of sized members

For types and members whose sizes are constant, we
could benefit greatly from traversing all members
from the root and noting how many bytes are known at
compile time to be needed for N nodes, filling those,
then loading the variable-size nodes, and repeating
until all the data is read or an error occurs. However,
this would require either reworking most of the core
code in lzfmt.hpp or changing the API, so it remains
a plan until the API is stabilized or reworked completely.

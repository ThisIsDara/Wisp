#pragma once
#include "core/AppEntry.h"

// Pure default-rule matcher (05.1 research Pattern 1 / Don't Hand-Roll) —
// one entry point, no Qt object, no state. NOT a ranker: FuzzyMatcher is
// a subsequence ladder and must NOT be reused for semantic curation
// matching. User overrides are the CALLER's concern (shownIds wins before
// this is consulted — see AppCatalog::markCurated, 05.1-02).
//
// ALLOWLIST semantics (05.1 checkpoint fix): the default catalog is a
// curated allowlist of ~280 well-known popular apps (game launchers,
// famous games, messengers, media, browsers, productivity/dev tools,
// VPNs, utilities, well-known Store apps). matches() returns TRUE when a
// default rule hides the entry — i.e. when the entry is NOT on the
// allowlist. An entry is allowlisted when its displayName OR its
// targetPath basename (Lnk rows; no extension) contains a curated token
// at a word boundary, case-insensitively. UWP/Store apps match by name
// only (no path). Source::File rows are never curated (CUR-04 escape
// hatch).
namespace CurationRules {
bool matches(const AppEntry &e);   // true when a DEFAULT rule hides the entry
}

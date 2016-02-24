// Copyright 2010-present Greg Hurrell. All rights reserved.
// Licensed under the terms of the BSD 2-clause license.

#include <float.h> /* for DBL_MAX */
#include "match.h"
#include "ext.h"
#include "ruby_compat.h"

#define NON_MATCH -1e9
#define UNSET DBL_MAX

// Use a struct to make passing params during recursion easier.
typedef struct {
    char    *haystack_p;            // Pointer to the path string to be searched.
    long    haystack_len;           // Length of same.
    char    *needle_p;              // Pointer to search string (needle).
    long    needle_len;             // Length of same.
    char    *rightmost_match_p;     // Rightmost match for each char in needle.
    double  max_score_per_char;
    int     always_show_dot_files;  // Boolean.
    int     never_show_dot_files;   // Boolean.
    int     case_sensitive;         // Boolean.
    int     compute_all_scorings;   // Boolean.
    double  *memo;                  // Memoization.
} matchinfo_t;

double recursive_match(
    matchinfo_t *m,    // Sharable meta-data.
    long haystack_idx, // Where in the path string to start.
    long needle_idx    // Where in the needle string to start.
) {
    double score_for_char;
    long i, distance;
    double score = NON_MATCH;

    // Do we have a memoized result we can return?
    double *memoized = &m->memo[needle_idx * m->needle_len + haystack_idx];
    if (*memoized != UNSET) {
        return *memoized;
    } else if (needle_idx == m->needle_len) {
        return *memoized = 0.0;
    }

    char c = m->needle_p[needle_idx];

    for (i = haystack_idx; i <= m->rightmost_match_p[needle_idx] ; i++) {
        char d = m->haystack_p[i];
        if (d == '.') {
            if (i == 0 || m->haystack_p[i - 1] == '/') { // This is a dot-file.
                int dot_search = c == '.'; // Searching for a dot.
                if (
                    m->never_show_dot_files ||
                    (!dot_search && !m->always_show_dot_files)
                ) {
                    return *memoized = NON_MATCH;
                }
            }
        } else if (d >= 'A' && d <= 'Z' && !m->case_sensitive) {
            d += 'a' - 'A'; // Add 32 to downcase.
        }

        if (c == d) {
            // Calculate score.
            score_for_char = m->max_score_per_char;
            distance = i - haystack_idx;

            if (distance > 1) {
                double factor = 1.0;
                char last = m->haystack_p[i - 1];
                char curr = m->haystack_p[i]; // Case matters, so get again.
                if (last == '/') {
                    factor = 0.9;
                } else if (
                    last == '-' ||
                    last == '_' ||
                    last == ' ' ||
                    (last >= '0' && last <= '9')
                ) {
                    factor = 0.8;
                } else if (
                    last >= 'a' && last <= 'z' &&
                    curr >= 'A' && curr <= 'Z'
                ) {
                    factor = 0.8;
                } else if (last == '.') {
                    factor = 0.7;
                } else {
                    // If no "special" chars behind char, factor diminishes
                    // as distance from last matched char increases.
                    factor = (1.0 / distance) * 0.75;
                }
                score_for_char *= factor;
            }

            double new_score =
                score_for_char +
                recursive_match(m, i + 1, needle_idx + 1);
            if (new_score > score) {
                score = new_score;
                if (!m->compute_all_scorings) {
                    break;
                }
            }
        }
    }
    return *memoized = score;
}

double calculate_match(
    VALUE haystack,
    VALUE needle,
    VALUE case_sensitive,
    VALUE always_show_dot_files,
    VALUE never_show_dot_files,
    VALUE compute_all_scorings
) {
    matchinfo_t m;
    long i, max;
    double score            = 1.0;
    m.haystack_p            = RSTRING_PTR(haystack);
    m.haystack_len          = RSTRING_LEN(haystack);
    m.needle_p              = RSTRING_PTR(needle);
    m.needle_len            = RSTRING_LEN(needle);
    m.rightmost_match_p     = NULL;
    m.max_score_per_char    = (1.0 / m.haystack_len + 1.0 / m.needle_len) / 2;
    m.always_show_dot_files = always_show_dot_files == Qtrue;
    m.never_show_dot_files  = never_show_dot_files == Qtrue;
    m.case_sensitive        = (int)case_sensitive;
    m.compute_all_scorings  = compute_all_scorings == Qtrue;

    // Special case for zero-length search string.
    if (m.needle_len == 0) {
        // Filter out dot files.
        if (!m.always_show_dot_files) {
            for (i = 0; i < m.haystack_len; i++) {
                char c = m.haystack_p[i];
                if (c == '.' && (i == 0 || m.haystack_p[i - 1] == '/')) {
                    return 0.0;
                }
            }
        }
    } else if (m.haystack_len > 0) { // Normal case.
        // Pre-scan string to see if it matches at all (short-circuits).
        // Record rightmost match match for each character (used to prune search space).
        char rightmost_match_p[m.needle_len];
        long rightmost_index = m.haystack_len;
        for (i = m.needle_len - 1; i >= 0; i--) {
            char c = m.needle_p[i];
            while (--rightmost_index >= 0) {
                char d = m.haystack_p[rightmost_index];
                if (!m.case_sensitive && d >= 'A' && d <= 'Z') {
                    d = d + ('a' - 'A');
                }
                if (c == d) {
                    rightmost_match_p[i] = rightmost_index;
                    break;
                }
            }
            if (rightmost_index < 0) {
                return 0.0;
            }
        }
        m.rightmost_match_p = rightmost_match_p;

        // Prepare for memoization.
        double memo[m.haystack_len * m.needle_len];
        for (i = 0, max = m.haystack_len * m.needle_len; i < max; i++) {
            memo[i] = UNSET;
        }
        m.memo = memo;

        score = recursive_match(&m, 0, 0);
    }
    return score == NON_MATCH ? 0.0 : score;
}

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <string>
#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <vector>

using namespace std;

enum Operation : int8_t { EQUAL=0, INSERT=1, DELETE=2 };

inline char op2chr(Operation op) {
    switch (op) {
        case DELETE:
            return '-';
        case INSERT:
            return '+';
        case EQUAL:
            return '=';
        default:
            return '?';
    }
}

/*
 * Computes the difference between two texts to create a patch.
 * Also contains the behaviour settings.
 */
class MyersDiff {
   public:
    // Defaults.
    // Set these on your diff_match_patch instance to override the defaults.

    /**
     * Number of milliseconds to map a diff before giving up (0 for infinity).
     */
    long Diff_Timeout = 1000;
    /**
     * Cost of an empty edit operation in terms of edit characters.
     */
    uint16_t Diff_EditCost = 4;
    /**
     * At what point is no match declared (0.0 = perfection, 1.0 = very loose).
     */
    float Match_Threshold = 0.5f;
    /**
     * How far to search for a match (0 = exact location, 1000+ = broad match).
     * A match this many characters away from the expected location will add
     * 1.0 to the score (0.0 is a perfect match).
     */
    int Match_Distance = 1000;
    /**
     * When deleting a large block of text (over ~64 characters), how close do
     * the contents have to be to match the expected contents. (0.0 =
     * perfection, 1.0 = very loose).  Note that Match_Threshold controls how
     * closely the end points of a delete need to match.
     */
    float Patch_DeleteThreshold = 0.5f;
    /**
     * Chunk size for context length.
     */
    uint16_t Patch_Margin = 4;

    int get_ans() { return ans; }

   public:
    using Time = chrono::time_point<chrono::steady_clock>;

    struct Range {
        const unsigned char* from, * till;
        Range(const unsigned char* begin, const unsigned char* end) : from{begin}, till{end} {}
        explicit Range(const unsigned char *str, int size)
            : from{str}, till{str+size} {}
        bool operator==(Range b) const {
            if (till - from != b.till - b.from) return false;
            for (auto i = from, j = b.from; i < till; ++i, ++j)
                if (*i != *j) return false;
            return true;
        }
        int size() const { return till - from; }
        Range substr(int start) const {
            assert(start <= size());
            return Range{from + start, till};
        }
        Range substr(int start, int end) const {
            assert(end >= start);
            assert(start <= size());
            if (end >= size()) end = size();
            return Range{from + start, from + end};
        }
        unsigned char operator[](int idx) const {
            assert(idx < size());
            return *(from + idx);
        }
        int find(Range b) const {
            auto at = std::search(from, till, b.from, b.till);
            return at < till ? at - from : -1;
        }
    };

   private:
    const unsigned char *text1;
    int size1;
    const unsigned char *text2;
    int size2;
    int   ans;

   public:
    MyersDiff(const unsigned char* original_text, int len1, const unsigned char* changed_text, int len2)
        : text1{original_text}, size1(len1), text2{changed_text}, size2(len2), ans(0) {
        diff_main(Range{text1, size1}, Range{text2, size2});
    }

    /**
     * Find the differences between two texts.
     * @return std::vector of Diff objects.
     */
    void diff_main(Range text1, Range text2) {
        // Set a deadline by which time the diff must be complete.
        Time deadline;
        if (Diff_Timeout <= 0) {
            deadline = Time::max();
        } else {
            deadline = chrono::steady_clock::now() +
                       chrono::milliseconds(Diff_Timeout);
        }
        return diff_main(text1, text2, deadline);
    }

    /**
     * Find the differences between two texts.  Simplifies the problem by
     * stripping any common prefix or suffix off the texts before diffing.
     * @param deadline Time when the diff should be complete by.  Used
     *     internally for recursive calls.  Users should set DiffTimeout
     * instead.
     * @return std::vector of Diff objects.
     */
    void diff_main(Range text1, Range text2, Time deadline) {
        // Check for equality (speedup).
        if (text1 == text2) {
            if (text1.size() != 0) {
                ans += text1.size();
            }
            return;
        }

        // Trim off common prefix (speedup).
        int commonlength = diff_commonPrefix(text1, text2);
        Range commonprefix = text1.substr(0, commonlength);
        text1 = text1.substr(commonlength);
        text2 = text2.substr(commonlength);

        // Trim off common suffix (speedup).
        commonlength = diff_commonSuffix(text1, text2);
        Range commonsuffix = text1.substr(text1.size() - commonlength);
        text1 = text1.substr(0, text1.size() - commonlength);
        text2 = text2.substr(0, text2.size() - commonlength);

        // Compute the diff on the middle block.
        diff_compute(text1, text2, deadline);

        // Restore the prefix and suffix.
        if (commonprefix.size() != 0) {
            ans += commonprefix.size();
        }
        if (commonsuffix.size() != 0) {
            ans += commonsuffix.size();
        }

        // TODO diff_cleanupMerge(diffs);
        return;
    }

    /**
     * Find the differences between two texts.  Assumes that the texts do not
     * have any common prefix or suffix.
     * @param text1 Old string to be diffed.
     * @param text2 New string to be diffed.
     * @param checklines Speedup flag.  If false, then don't run a
     *     line-level diff first to identify the changed areas.
     *     If true, then run a faster slightly less optimal diff.
     * @param deadline Time when the diff should be complete by.
     * @return std::vector of Diff objects.
     */
    void diff_compute(Range text1, Range text2, Time deadline) {
        if (text1.size() == 0) {
            // Just add some text (speedup).
            return;
        }

        if (text2.size() == 0) {
            // Just delete some text (speedup).
            return;
        }

        Range longtext = text1.size() > text2.size() ? text1 : text2;
        Range shorttext = text1.size() > text2.size() ? text2 : text1;
        int i = longtext.find(shorttext);
        if (i != -1) {
            // Shorter text is inside the longer text (speedup).
            ans += shorttext.size();
            return;
        }

        if (shorttext.size() == 1) {
            // Single character string.
            // After the previous speedup, the character can't be an equality.
            return;
        }

        return diff_bisect(text1, text2, deadline);
    }

    /**
     * Find the 'middle snake' of a diff, split the problem in two
     * and return the recursively constructed diff.
     * See Myers 1986 paper: An O(ND) Difference Algorithm and Its Variations.
     * @param text1 Old string to be diffed.
     * @param text2 New string to be diffed.
     * @param deadline Time at which to bail if not yet complete.
     * @return std::vector of Diff objects.
     */
    void diff_bisect(Range text1, Range text2, Time deadline) {
        // Cache the text lengths to prevent multiple calls.
        int text1_length = text1.size();
        int text2_length = text2.size();
        int max_d = (text1_length + text2_length + 1) / 2;
        int v_offset = max_d;
        int v_length = 2 * max_d;
        vector<int> v1;
        v1.resize(v_length);
        vector<int> v2;
        v2.resize(v_length);
        for (int x = 0; x < v_length; x++) {
            v1[x] = -1;
            v2[x] = -1;
        }
        v1[v_offset + 1] = 0;
        v2[v_offset + 1] = 0;
        int delta = text1_length - text2_length;
        // If the total number of characters is odd, then the front path will
        // collide with the reverse path.
        bool front = (delta % 2 != 0);
        // Offsets for start and end of k loop.
        // Prevents mapping of space beyond the grid.
        int k1start = 0;
        int k1end = 0;
        int k2start = 0;
        int k2end = 0;
        for (int d = 0; d < max_d; d++) {
            // Bail out if deadline is reached.
            /* TODO if (System.currentTimeMillis() > deadline) {
              break;
            } */

            // Walk the front path one step.
            for (int k1 = -d + k1start; k1 <= d - k1end; k1 += 2) {
                int k1_offset = v_offset + k1;
                int x1;
                if (k1 == -d ||
                    (k1 != d && v1[k1_offset - 1] < v1[k1_offset + 1])) {
                    x1 = v1[k1_offset + 1];
                } else {
                    x1 = v1[k1_offset - 1] + 1;
                }
                int y1 = x1 - k1;
                while (x1 < text1_length && y1 < text2_length &&
                       text1[x1] == text2[y1]) {
                    x1++;
                    y1++;
                }
                v1[k1_offset] = x1;
                if (x1 > text1_length) {
                    // Ran off the right of the graph.
                    k1end += 2;
                } else if (y1 > text2_length) {
                    // Ran off the bottom of the graph.
                    k1start += 2;
                } else if (front) {
                    int k2_offset = v_offset + delta - k1;
                    if (k2_offset >= 0 && k2_offset < v_length &&
                        v2[k2_offset] != -1) {
                        // Mirror x2 onto top-left coordinate system.
                        int x2 = text1_length - v2[k2_offset];
                        if (x1 >= x2) {
                            // Overlap detected.
                            return diff_bisectSplit(text1, text2, x1, y1,
                                                    deadline);
                        }
                    }
                }
            }

            // Walk the reverse path one step.
            for (int k2 = -d + k2start; k2 <= d - k2end; k2 += 2) {
                int k2_offset = v_offset + k2;
                int x2;
                if (k2 == -d ||
                    (k2 != d && v2[k2_offset - 1] < v2[k2_offset + 1])) {
                    x2 = v2[k2_offset + 1];
                } else {
                    x2 = v2[k2_offset - 1] + 1;
                }
                int y2 = x2 - k2;
                while (x2 < text1_length && y2 < text2_length &&
                       text1[text1_length - x2 - 1] ==
                           text2[text2_length - y2 - 1]) {
                    x2++;
                    y2++;
                }
                v2[k2_offset] = x2;
                if (x2 > text1_length) {
                    // Ran off the left of the graph.
                    k2end += 2;
                } else if (y2 > text2_length) {
                    // Ran off the top of the graph.
                    k2start += 2;
                } else if (!front) {
                    int k1_offset = v_offset + delta - k2;
                    if (k1_offset >= 0 && k1_offset < v_length &&
                        v1[k1_offset] != -1) {
                        int x1 = v1[k1_offset];
                        int y1 = v_offset + x1 - k1_offset;
                        // Mirror x2 onto top-left coordinate system.
                        x2 = text1_length - x2;
                        if (x1 >= x2) {
                            // Overlap detected.
                            return diff_bisectSplit(text1, text2, x1, y1,
                                                    deadline);
                        }
                    }
                }
            }
        }
        return;
    }

    /**
     * Given the location of the 'middle snake', split the diff in two parts
     * and recurse.
     * @param text1 Old string to be diffed.
     * @param text2 New string to be diffed.
     * @param x Index of split point in text1.
     * @param y Index of split point in text2.
     * @param deadline Time at which to bail if not yet complete.
     * @return std::vector of Diff objects.
     */
    void diff_bisectSplit(Range text1, Range text2, int x, int y,
                           Time deadline) {
        Range text1a = text1.substr(0, x);
        Range text2a = text2.substr(0, y);
        Range text1b = text1.substr(x);
        Range text2b = text2.substr(y);

        // Compute both diffs serially.
        diff_main(text1a, text2a, deadline);
        diff_main(text1b, text2b, deadline);

        return;
    }

    /**
     * Determine the common prefix of two strings
     * @param text1 First string.
     * @param text2 Second string.
     * @return The number of characters common to the start of each string.
     */
    int diff_commonPrefix(Range text1, Range text2) {
        // Performance analysis: https://neil.fraser.name/news/2007/10/09/
        int n = std::min(text1.size(), text2.size());
        for (int i = 0; i < n; i++) {
            if (text1[i] != text2[i]) {
                return i;
            }
        }
        return n;
    }

    /**
     * Determine the common suffix of two strings
     * @param text1 First string.
     * @param text2 Second string.
     * @return The number of characters common to the end of each string.
     */
    int diff_commonSuffix(Range text1, Range text2) {
        // Performance analysis: https://neil.fraser.name/news/2007/10/09/
        int text1_length = text1.size();
        int text2_length = text2.size();
        int n = std::min(text1_length, text2_length);
        for (int i = 1; i <= n; i++) {
            if (text1[text1_length - i] != text2[text2_length - i]) {
                return i - 1;
            }
        }
        return n;
    }
};

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "diff_binary: arguments error.\n\t\t diff_binary acc_file decode_file");
        return 1;
    }
    char* acc_file = argv[1];
    char* dec_file = argv[2];
    FILE* acc = fopen(acc_file, "rb");
    if (!acc) {
        fprintf(stderr, "diff_binary: cannot open: %s\n", acc_file);
        return 1;
    }
    FILE* dec = fopen(dec_file, "rb");
    if (!dec) {
        fprintf(stderr, "diff_binary: cannot open: %s\n", dec_file);
        fclose(acc);
        return 1;
    }
    fseek(acc, 0, SEEK_END);
    int acc_size = ftell(acc);
    fseek(acc, 0, SEEK_SET);
    fseek(dec, 0, SEEK_END);
    int dec_size = ftell(dec);
    fseek(dec, 0, SEEK_SET);

    unsigned char* acc_bi = new unsigned char[acc_size];
    unsigned char* dec_bi = new unsigned char[dec_size];
    fread(acc_bi, acc_size, 1, acc);
    fread(dec_bi, dec_size, 1, dec);

    // int* common = new int[acc_size+1]();
    // for (int i = 1; i <= dec_size; ++i) {
    //     int prepre = common[0];
    //     for (int j = 1; j <= acc_size; ++j) {
    //         int pre = common[j];
    //         common[j] = std::max(prepre + ((acc_bi[j-1]==dec_bi[i-1])?1:0),
    //                              std::max(pre, common[j-1]));
    //         prepre = pre;
    //     }
    // }

    int common = 0;
    MyersDiff diff{acc_bi, acc_size, dec_bi, dec_size};
    common = diff.get_ans();
    printf("acc_size: %d dec_size: %d common_size: %d\n", acc_size, dec_size, common);
    fclose(acc);
    fclose(dec);
    delete[] acc_bi;
    delete[] dec_bi;
    return 0;
}

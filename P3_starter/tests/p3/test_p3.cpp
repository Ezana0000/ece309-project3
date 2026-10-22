// tests/p3/test_p3.cpp — YOUR P3 TEST SUITE.
//
// The five tests below are worked examples. Keep them, and add your own
// until you have AT LEAST 15 (spec §6 lists what must be covered).
// Name every test <area>_<what>: reg_ for the registry, ws_ for the
// workspace, calc_ for the calculator.
//
// Build & run:   cmake --build build && ./build/test_p3
// One area:      ./build/test_p3 reg_        (names starting with "reg_")
// One test:      ./build/test_p3 =reg_add_and_find

#include "../mini_test.h"
#include "../test_helpers.h"     // TempDir, write_text, args1, ...

#include "tools/calculator.h"
#include "tools/tool_registry.h"
#include "tools/workspace.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// A tool whose name you choose — use it to fill the registry in tests.
class NamedTool : public Tool {
public:
    explicit NamedTool(std::string n) : n_(std::move(n)), schema_(JsonValue::object()) {}
    std::string name() const override { return n_; }
    std::string description() const override { return "test tool"; }
    const JsonValue& schema() const override { return schema_; }
    ToolResult invoke(const JsonValue&, ToolContext&) override { return ToolResult::success(n_); }
private:
    std::string n_;
    JsonValue   schema_;   // an empty JSON object: {}
};

// Tokenize with the provided tokenizer, then run YOUR calc_evaluate.
bool eval(const std::string& expr, double* value, std::string* error) {
    std::vector<CalcToken> tokens;
    if (!calc_tokenize(expr, &tokens, error)) return false;
    return calc_evaluate(tokens, value, error);
}

}  // namespace

// ---------------------------------------------------------------- examples

TEST(reg_add_and_find) {
    ToolRegistry r;
    auto t = std::make_unique<NamedTool>("alpha");
    Tool* raw = t.get();                        // remember the address before moving
    REQUIRE(r.add(std::move(t)));
    CHECK(r.size() == 1);
    CHECK(r.find("alpha") == raw);              // same object, not a copy
    CHECK(r.find("beta") == nullptr);
}

TEST(reg_duplicate_keeps_original) {
    ToolRegistry r;
    auto first = std::make_unique<NamedTool>("alpha");
    Tool* original = first.get();
    REQUIRE(r.add(std::move(first)));
    CHECK(!r.add(std::make_unique<NamedTool>("alpha")));
    CHECK(r.find("alpha") == original);
    CHECK(r.size() == 1);
}

TEST(ws_rejects_parent_escape) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    REQUIRE(ws.resolve("notes/todo.txt", &out, &reason));   // sanity: accepts *something*
    CHECK(out == ws.root() / "notes" / "todo.txt");
    CHECK(!ws.resolve("../etc/passwd", &out, &reason));
    CHECK(!reason.empty());
}

TEST(calc_precedence) {
    double v = 0;
    std::string err;
    REQUIRE(eval("2 + 3 * 4", &v, &err));
    CHECK(v == 14);
}

TEST(calc_division_by_zero) {
    double v = 0;
    std::string err;
    CHECK(!eval("1 / 0", &v, &err));
    CHECK(err == "division by zero");
}

// ------------------------------------------------------------- registry

TEST(reg_empty_table) {
    ToolRegistry r;
    CHECK(r.size() == 0);
    CHECK(r.bucket_count() >= 1 && r.bucket_count() <= 64);
    CHECK(r.load_factor() == 0.0);
    CHECK(r.max_probe_length() == 0);
    CHECK(r.find("anything") == nullptr);
}

TEST(reg_null_rejected) {
    ToolRegistry r;
    CHECK(!r.add(nullptr));
    CHECK(r.size() == 0);
}

TEST(reg_size_tracks_adds) {
    ToolRegistry r;
    REQUIRE(r.add(std::make_unique<NamedTool>("a")));
    CHECK(r.size() == 1);
    REQUIRE(r.add(std::make_unique<NamedTool>("b")));
    CHECK(r.size() == 2);
    CHECK(!r.add(std::make_unique<NamedTool>("a")));   // duplicate does not count
    CHECK(!r.add(nullptr));                              // null does not count
    CHECK(r.size() == 2);
    CHECK(r.find("c") == nullptr);
}

TEST(reg_for_each_visits_each_once) {
    ToolRegistry r;
    const std::vector<std::string> names = {"read_file", "write_file", "list_dir",
                                            "calculator", "alpha", "beta"};
    for (const std::string& n : names) REQUIRE(r.add(std::make_unique<NamedTool>(n)));

    // Count how many times each name shows up.
    std::vector<int> seen(names.size(), 0);
    int calls = 0;
    r.for_each([&](const Tool& t) {
        ++calls;
        for (std::size_t i = 0; i < names.size(); ++i)
            if (t.name() == names[i]) ++seen[i];
    });
    CHECK(calls == static_cast<int>(names.size()));
    for (int c : seen) CHECK(c == 1);
}

TEST(reg_hash_is_order_sensitive) {
    CHECK(ToolRegistry::hash("ab") != ToolRegistry::hash("ba"));
    CHECK(ToolRegistry::hash("tool_1234") != ToolRegistry::hash("tool_1243"));
    CHECK(ToolRegistry::hash("calculator") == ToolRegistry::hash("calculator"));
}

TEST(reg_10000_tools) {
    ToolRegistry r;
    auto first = std::make_unique<NamedTool>("tool_0");
    Tool* first_ptr = first.get();
    REQUIRE(r.add(std::move(first)));

    for (int i = 1; i < 10000; ++i) {
        REQUIRE(r.add(std::make_unique<NamedTool>("tool_" + std::to_string(i))));
        CHECK(r.load_factor() <= 1.0);   // must hold after every add
    }
    CHECK(r.size() == 10000);

    // Every name is still there, and the first pointer survived all the resizes.
    for (int i = 0; i < 10000; ++i)
        CHECK(r.find("tool_" + std::to_string(i)) != nullptr);
    CHECK(r.find("tool_0") == first_ptr);
    CHECK(r.max_probe_length() <= 64);

    std::printf("    FNV-1a + chaining: bucket_count=%zu load_factor=%.3f max_probe_length=%zu\n",
                r.bucket_count(), r.load_factor(), r.max_probe_length());
}

TEST(reg_alternative_hash) {
    // Build a real table first so we use the same final bucket count.
    ToolRegistry r;
    for (int i = 0; i < 10000; ++i)
        REQUIRE(r.add(std::make_unique<NamedTool>("tool_" + std::to_string(i))));
    const std::size_t buckets = r.bucket_count();

    // Weak hash s[0] + s.size(): count how many names land in each bucket.
    std::vector<int> counts(buckets, 0);
    for (int i = 0; i < 10000; ++i) {
        std::string s = "tool_" + std::to_string(i);
        std::size_t weak = static_cast<unsigned char>(s[0]) + s.size();
        ++counts[weak % buckets];
    }
    int weak_longest = 0;
    for (int c : counts)
        if (c > weak_longest) weak_longest = c;

    std::printf("    buckets=%zu  FNV-1a longest chain=%zu  weak hash longest chain=%d\n",
                buckets, r.max_probe_length(), weak_longest);
    CHECK(static_cast<std::size_t>(weak_longest) > r.max_probe_length());
}

// ------------------------------------------------------------- workspace

namespace {
// Runs resolve and checks it was accepted with the expected result.
void expect_accept(const Workspace& ws, const std::string& in, const fs::path& want) {
    fs::path out;
    std::string reason;
    CHECK(ws.resolve(in, &out, &reason));
    CHECK(out == want);
}

// Runs resolve and checks it was rejected with a reason.
void expect_reject(const Workspace& ws, const std::string& in) {
    fs::path out;
    std::string reason;
    CHECK(!ws.resolve(in, &out, &reason));
    CHECK(!reason.empty());
}
}  // namespace

TEST(ws_accepts_normal_paths) {
    TempDir d;
    Workspace ws(d.path());
    expect_accept(ws, "notes/todo.txt", ws.root() / "notes" / "todo.txt");
    expect_accept(ws, "./notes//todo.txt", ws.root() / "notes" / "todo.txt");
    expect_accept(ws, "new/dir/file.txt", ws.root() / "new" / "dir" / "file.txt");
    expect_accept(ws, "my..notes.txt", ws.root() / "my..notes.txt");
}

TEST(ws_accepts_dot_and_folded_dotdot) {
    TempDir d;
    Workspace ws(d.path());
    expect_accept(ws, ".", ws.root());
    expect_accept(ws, "a/../b", ws.root() / "b");
    expect_accept(ws, "a/..", ws.root());
}

TEST(ws_does_not_need_file_to_exist) {
    TempDir d;
    Workspace ws(d.path());
    expect_accept(ws, "new/dir/file.txt", ws.root() / "new" / "dir" / "file.txt");
    CHECK(!fs::exists(ws.root() / "new"));   // resolve must not create anything
}

TEST(ws_rejects_empty_and_nul) {
    TempDir d;
    Workspace ws(d.path());
    expect_reject(ws, "");
    expect_reject(ws, std::string("notes") + '\0' + "/x");   // build it so the NUL survives
}

TEST(ws_rejects_absolute) {
    TempDir d;
    Workspace ws(d.path());
    expect_reject(ws, "/etc/passwd");
    expect_reject(ws, (ws.root() / "notes").string());   // absolute even though it is inside
}

TEST(ws_rejects_dotdot) {
    TempDir d;
    Workspace ws(d.path());
    expect_reject(ws, "..");
    expect_reject(ws, "../etc/passwd");
    expect_reject(ws, "a/../../b");
    // Climbs out and comes back in: still rejected.
    const std::string back_in = "notes/../../" + ws.root().filename().string() + "/notes/todo.txt";
    expect_reject(ws, back_in);
}

// ------------------------------------------------------------- calculator

namespace {
// Evaluates and checks the result matches.
void expect_value(const std::string& expr, double want) {
    double v = 0;
    std::string err;
    CHECK(eval(expr, &v, &err));
    CHECK(v == want);
}
}  // namespace

TEST(calc_spec_table) {
    expect_value("2 + 3 * 4", 14);
    expect_value("10 - 4 - 3", 3);
    expect_value("100 / 10 / 5", 2);
    expect_value("(2 + 3) * 4", 20);
    expect_value("2 * (3 + 4) - 5", 9);
    expect_value("-3 * -3", 9);
    expect_value("7 / 2", 3.5);
}

TEST(calc_left_to_right) {
    expect_value("10 - 4 - 3", 3);
    expect_value("100 / 10 / 5", 2);
    expect_value("8 / 4 * 2", 4);   // same precedence goes left to right
}

TEST(calc_nested_parens_and_decimals) {
    expect_value("((1 + 2) * (3 + 4)) / 7", 3);
    expect_value("1.5 * 4 - 0.5", 5.5);
    expect_value("(-1) + 2 * -3", -7);
}

TEST(calc_divide_by_zero_in_parens) {
    double v = 0;
    std::string err;
    CHECK(!eval("1 / (2 - 2)", &v, &err));
    CHECK(err == "division by zero");
}

TEST(calc_very_long_inputs) {
    // 20,000 terms and 5,000 nested parentheses: loops, not recursion.
    std::string sum = "1";
    for (int i = 1; i < 20000; ++i) sum += "+1";
    expect_value(sum, 20000);

    std::string nested(5000, '(');
    nested += "7";
    nested += std::string(5000, ')');
    expect_value(nested, 7);
}

TEST(calc_empty_tokens_does_not_crash) {
    // Not something the tokenizer produces, but it must fail cleanly.
    std::vector<CalcToken> none;
    double v = 0;
    std::string err;
    CHECK(!calc_evaluate(none, &v, &err));
    CHECK(!err.empty());
}

int main(int argc, char** argv) { return mt::run_all(argc, argv); }

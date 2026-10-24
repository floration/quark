// A small fixture for the tests editor's parser: the shapes tests/tests.cpp uses, plus the awkward ones.
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define OTHERCHECK(c) do { if (!(c)) g_fail++; } while (0)
static int g_portShift = 0;
static unsigned short TP(int port) { return (unsigned short)(port + g_portShift); }

namespace {
// Inside a namespace: still indexed.
void testInNamespace() { CHECK(1 + 1 == 2); }
}

// Strings and comments with braces must not confuse the brace matching.
// Second line of the doc.
static void testBraces() {
    const char* s = "{ not a brace } \" still a string {";
    char c = '{';
    const char* raw = R"x(a raw string with } and " and { )x";
    /* a block comment { */
    CHECK(s[0] == '{' && c == '{' && raw[0] == 'a');
    CHECK(std::string("}").size() ==
          1);   // a multi-line check
}

/* Block doc
   for testPorts. */
static void testPorts(unsigned short port) {
    CHECK(port > 0);
    CHECK(port + 3 > port);
}
static void testOther(unsigned short port) { OTHERCHECK(port != 0); testBraces(); }
static int helperCount() { return 3; }
static void testNeverRun() { CHECK(helperCount() == 3); }
int launcherTests() { int f = 0; return f; }

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "braces") { testBraces(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "ports") { testPorts(TP(100)); return 0; }    if (argc >= 2 && std::string(argv[1]) == "toolish") { printf("%d\n", helperCount()); return 0; }
    if (argc >= 2 && std::string(argv[1]) == "bench")
        return helperCount() > 2 ? 0 : 1;
    struct T { const char* name; std::function<void()> run; };
    std::vector<T> par = {   // a comment { with a brace
        {"alpha", [] { uint16_t p = TP(200); testPorts(p); testOther(p + 1); }},
        {"beta", [] {
            testInNamespace();
            printf("beta {\n");
            { unsigned short q = TP(300); testPorts(q); }
            g_fail += launcherTests();
        }},
        {"gamma", [] { testPorts(TP(203)); testMissing(); }},
    };
    std::vector<T> alone = {
        {"delta", [] { testBraces(); }},
    };
    if (argc >= 2 && std::string(argv[1]) == "groups") {
        for (auto* list : {&par, &alone}) for (auto& g : *list) printf("%s\n", g.name);
        return 0;
    }
    for (auto& t : par) t.run();
    for (size_t i = 0; i < par.size(); i++) par[i].run();
    for (auto& t : alone) t.run();
    return g_fail ? 1 : 0;
}

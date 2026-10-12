// The light scan's slice-by-slice walk (env/SceneWalk.hpp) over stand-in lists: every node visited once per pass
// across slices, each slice within its budget, a pass that loses the node it stopped at starting over, and broken
// lists ending.
#include "env/SceneWalk.hpp"

#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace
{
    namespace walk = wxl_livingazeroth::lights::walk;

    int g_failures = 0;
    void Check(bool ok, const std::string& what)
    {
        std::printf("%s  %s\n", ok ? "ok   " : "FAIL ", what.c_str());
        if (!ok) ++g_failures;
    }

    struct Node { Node* next = nullptr; bool listed = true; int id = 0; };

    // A list of `n` nodes; visiting one costs `costMs` on a stand-in clock.
    struct Hooks
    {
        std::vector<Node>* nodes = nullptr;
        Node* head = nullptr;
        double clock = 0.0, costMs = 0.01;
        std::vector<int> visits;
        const void* Head() { return head; }
        const void* Next(const void* node) { return static_cast<const Node*>(node)->next; }
        bool StillListed(const void* node) { return static_cast<const Node*>(node)->listed; }
        void Visit(const void* node) { visits.push_back(static_cast<const Node*>(node)->id); clock += costMs; }
        double NowMs() { return clock; }
    };

    std::vector<Node> MakeList(int n)
    {
        std::vector<Node> nodes(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) { nodes[i].id = i; nodes[i].next = i + 1 < n ? &nodes[i + 1] : nullptr; }
        return nodes;
    }
}

int main()
{
    // 1. A Dalaran-sized list (30,349 models) at 0.01 ms a model, 0.5 ms a slice: every model once, in order, over
    //    the slices it takes, none running much past the budget.
    {
        std::vector<Node> nodes = MakeList(30349);
        Hooks h; h.nodes = &nodes; h.head = &nodes[0];
        walk::Walk w;
        int slices = 0;
        double worst = 0.0;
        bool ended = false;
        while (!ended && slices < 100000)
        {
            const double before = h.clock;
            ended = w.Slice(h, 0.5, 100000);
            worst = std::max(worst, h.clock - before);
            ++slices;
        }
        bool inOrder = static_cast<int>(h.visits.size()) == 30349;
        for (int i = 0; inOrder && i < 30349; ++i) inOrder = h.visits[i] == i;
        Check(ended && inOrder && w.Visited() == 30349 && w.Restarts() == 0 && !w.Active(),
              "30,349 models, each once and in order, over " + std::to_string(slices) + " slices");
        Check(worst <= 0.5 + walk::kCheckEvery * 0.01 + 1e-9,
              "each slice within the budget plus one clock check's worth: worst " + std::to_string(worst) + " ms");
        // The next call starts a new pass from the head.
        h.visits.clear();
        w.Slice(h, 0.5, 100000);
        Check(!h.visits.empty() && h.visits[0] == 0 && w.Active(), "the next slice starts a new pass at the head");
    }

    // 2. The node a slice stopped at is unlinked before the next: the pass starts over at the head and still ends
    //    having visited every node still listed.
    {
        std::vector<Node> nodes = MakeList(1000);
        Hooks h; h.nodes = &nodes; h.head = &nodes[0];
        walk::Walk w;
        const bool first = w.Slice(h, 0.5, 100000);   // 50 visits: stops at node 50
        const int stoppedAt = static_cast<int>(h.visits.size());
        nodes[stoppedAt - 1].next = &nodes[stoppedAt + 1];
        nodes[stoppedAt].listed = false;
        bool ended = false;
        for (int i = 0; i < 1000 && !ended; ++i) ended = w.Slice(h, 0.5, 100000);
        std::set<int> seen(h.visits.begin(), h.visits.end());
        bool all = true;
        for (int i = 0; i < 1000; ++i) if (i != stoppedAt && !seen.count(i)) all = false;
        Check(!first && ended && w.Restarts() == 1 && all && !seen.count(stoppedAt),
              "the stopped-at node unlinked: one restart, every listed node visited (" + std::to_string(h.visits.size()) + " visits)");
    }

    // 3. A cyclic list ends at the cap; an empty one at once; an unreadable next (nullptr) ends the pass.
    {
        std::vector<Node> nodes = MakeList(10);
        nodes[9].next = &nodes[3];
        Hooks h; h.nodes = &nodes; h.head = &nodes[0]; h.costMs = 0.0;
        walk::Walk w;
        const bool ended = w.Slice(h, 0.5, 500);
        Check(ended && w.Visited() == 500, "a cyclic list ends at the cap (500 visits)");

        Hooks empty;
        walk::Walk w2;
        Check(w2.Slice(empty, 0.5, 500) && w2.Visited() == 0 && empty.visits.empty(), "an empty list ends at once");
    }

    // 4. Abandoned (the world left): the next slice starts over.
    {
        std::vector<Node> nodes = MakeList(1000);
        Hooks h; h.nodes = &nodes; h.head = &nodes[0];
        walk::Walk w;
        w.Slice(h, 0.5, 100000);
        w.Abandon();
        h.visits.clear();
        w.Slice(h, 0.5, 100000);
        Check(!h.visits.empty() && h.visits[0] == 0 && w.Restarts() == 0, "abandoned, the next slice starts a new pass at the head");
    }

    std::printf(g_failures ? "FAILED (%d)\n" : "PASSED (0 failures)\n", g_failures);
    return g_failures ? 1 : 0;
}

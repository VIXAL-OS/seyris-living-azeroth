// Walking a long linked list a slice at a time (env/Lights' model scan), kept apart so the rules can be checked offline
// (tests/lights).
//
// The light scan walked the client's whole M2 scene list four times a second: 30,349 models in Dalaran, 6,312 of them
// within range, 5.3 ms a walk there and several times that in a crowd -- 5.4 ms a frame on average and a stutter
// every quarter second ("dalaran framerate seems like it sucks worse than usual", the user, 2026-10-12; at the coast
// it cost 2.8 ms a frame with one light in range). Now each frame walks only as far as a time budget allows and the
// next frame resumes where it stopped.
//
// The client links models in and out between frames. The node a slice stopped at is asked whether it is still listed
// before the walk resumes there; if not, the walk starts over from the head (the caller keeps what it already found
// and drops repeats). A pass visits at most `maxNodes` nodes in all, restarts included, so a broken (cyclic) list
// ends there.
#pragma once

namespace wxl_livingazeroth::lights::walk
{
    /// How many nodes go by between looks at the clock.
    constexpr unsigned kCheckEvery = 32;

    /// One walk over a list, slice by slice. Hooks (any type with these):
    ///   const void* Head();                    the list's first node (nullptr: empty or unreadable)
    ///   const void* Next(const void* node);    the node after it (nullptr: the end, or unreadable)
    ///   bool StillListed(const void* node);    whether a node a slice stopped at is still in the list
    ///   void Visit(const void* node);
    ///   double NowMs();
    class Walk
    {
    public:
        /// Walks on until `budgetMs` has gone by (looked at every kCheckEvery nodes) or the pass ends. True when the
        /// pass ended in this slice: the next call starts a new one.
        template <class Hooks>
        bool Slice(Hooks& hooks, double budgetMs, unsigned maxNodes)
        {
            if (!m_active)
            {
                m_active = true;
                m_visited = 0;
                m_restarts = 0;
                m_cursor = hooks.Head();
            }
            else if (m_cursor && !hooks.StillListed(m_cursor))
            {
                ++m_restarts;
                m_cursor = hooks.Head();
            }
            const double start = hooks.NowMs();
            unsigned sinceCheck = 0;
            while (m_cursor && m_visited < maxNodes)
            {
                hooks.Visit(m_cursor);
                ++m_visited;
                m_cursor = hooks.Next(m_cursor);
                if (++sinceCheck >= kCheckEvery)
                {
                    sinceCheck = 0;
                    if (m_cursor && hooks.NowMs() - start >= budgetMs) return false;
                }
            }
            m_active = false;
            m_cursor = nullptr;
            return true;
        }

        /// Drops a pass under way (the world left, the tables changed): the next slice starts a new one.
        void Abandon() { m_active = false; m_cursor = nullptr; }

        bool     Active() const { return m_active; }
        unsigned Visited() const { return m_visited; }    // this pass (or the last, once ended)
        unsigned Restarts() const { return m_restarts; }  // times this pass went back to the head

    private:
        bool        m_active = false;
        const void* m_cursor = nullptr;
        unsigned    m_visited = 0, m_restarts = 0;
    };
}

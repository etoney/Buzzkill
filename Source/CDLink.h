#pragma once
// ============================================================================
//  CASTLE DANGER LINK  (protocol 1)
//  Lets Castle Danger plugins in the same DAW (or on the same computer) find each
//  other and talk: The Master reads what each linked plugin is doing and can set
//  its controls. Fully offline: everything stays on 127.0.0.1.
//
//  The same file is copied into every Castle Danger plugin that links. Keep the
//  copies identical (bump kProtocol for incompatible changes).
//
//  How it works
//   * Each plugin binary has one Node (shared by all its instances).
//   * The first Node to start listens on 127.0.0.1:kPort and becomes the hub; every
//     other Node connects to it. If the hub's plugin is removed, the others notice and
//     one of them takes over within a second or two.
//   * The hub relays every message to everyone, so any instance can reach any other,
//     even when the DAW runs plugins in separate processes.
//   * Messages are small JSON objects: { type, from, kind, to, ... }.
//   * Everything runs on the message thread; nothing here touches the audio thread
//     except EnvTap, which is lock-free.
// ============================================================================
#include <juce_events/juce_events.h>
#include <map>
#include <memory>

namespace cdlink
{
constexpr int kProtocol = 1;
constexpr int kPort = 47391;
constexpr juce::uint32 kMagic = 0xCDA51701;

class Endpoint;

//==============================================================================
class Node : private juce::Timer
{
public:
    static std::shared_ptr<Node> get()
    {
        auto& w = weakSelf();
        if (auto s = w.lock()) return s;
        auto s = std::shared_ptr<Node> (new Node());
        w = s;
        return s;
    }
    // tests only: a second, independent Node, as if it lived in another plugin binary
    static std::shared_ptr<Node> createIsolatedForTesting() { return std::shared_ptr<Node> (new Node()); }

    ~Node() override
    {
        stopTimer();
        if (server) server->stop();
        server.reset();
        const juce::ScopedLock sl (clientLock);
        clients.clear();
        upstream.reset();
    }

    void add (Endpoint* e) { locals.addIfNotAlreadyThere (e); }
    void remove (Endpoint* e) { locals.removeFirstMatchingValue (e); }
    bool isHub() const { return server != nullptr; }
    bool isOnline() const { return server != nullptr || (upstream != nullptr && upstream->isConnected()); }

    // from a local endpoint
    void post (const juce::String& json, Endpoint* sender)
    {
        deliverLocal (json, sender);
        const juce::MemoryBlock mb (json.toRawUTF8(), json.getNumBytesAsUTF8());
        if (server)
        {
            const juce::ScopedLock sl (clientLock);
            for (auto* c : clients) if (c->isConnected()) c->sendMessage (mb);
        }
        else if (upstream && upstream->isConnected())
            upstream->sendMessage (mb);
    }

private:
    Node() { connectOrHost(); startTimer (1000); }

    static std::weak_ptr<Node>& weakSelf() { static std::weak_ptr<Node> w; return w; }

    struct Conn : public juce::InterprocessConnection
    {
        explicit Conn (Node& n) : juce::InterprocessConnection (true, kMagic), node (n) {}
        ~Conn() override { disconnect(); }
        void connectionMade() override {}
        void connectionLost() override { dead = true; }
        void messageReceived (const juce::MemoryBlock& mb) override { node.received (this, mb.toString()); }
        Node& node;
        std::atomic<bool> dead { false };
    };

    struct Server : public juce::InterprocessConnectionServer
    {
        explicit Server (Node& n) : node (n) {}
        juce::InterprocessConnection* createConnectionObject() override
        {
            auto* c = new Conn (node);
            const juce::ScopedLock sl (node.clientLock);
            node.clients.add (c);
            return c;
        }
        Node& node;
    };

    void connectOrHost()
    {
        upstream.reset();
        auto s = std::make_unique<Server> (*this);
        if (s->beginWaitingForSocket (kPort, "127.0.0.1")) { server = std::move (s); return; }
        auto c = std::make_unique<Conn> (*this);
        if (c->connectToSocket ("127.0.0.1", kPort, 250)) upstream = std::move (c);
    }

    void timerCallback() override
    {
        if (server)
        {
            const juce::ScopedLock sl (clientLock);
            for (int i = clients.size(); --i >= 0;)
                if (clients[i]->dead) clients.remove (i);
            return;
        }
        if (upstream == nullptr || upstream->dead || ! upstream->isConnected()) connectOrHost();
    }

    void received (Conn* from, const juce::String& json)
    {
        deliverLocal (json, nullptr);
        if (server)
        {
            const juce::MemoryBlock mb (json.toRawUTF8(), json.getNumBytesAsUTF8());
            const juce::ScopedLock sl (clientLock);
            for (auto* c : clients) if (c != from && c->isConnected()) c->sendMessage (mb);
        }
    }

    void deliverLocal (const juce::String& json, Endpoint* sender);

    std::unique_ptr<Server> server;
    juce::OwnedArray<Conn> clients;
    juce::CriticalSection clientLock;
    std::unique_ptr<Conn> upstream;
    juce::Array<Endpoint*> locals;
};

//==============================================================================
// One per plugin instance. kind: "master", "dominatrix", "chopshop", "servant", ...
class Endpoint
{
public:
    using Handler = std::function<void (const juce::var& msg)>;

    Endpoint (const juce::String& k, Handler h, std::shared_ptr<Node> n = {})
        : kind (k), handler (std::move (h)), node (n ? std::move (n) : Node::get())
    {
        id = juce::Uuid().toString().substring (0, 12);
        node->add (this);
    }
    ~Endpoint()
    {
        send (makeMessage ("bye"));
        node->remove (this);
    }

    const juce::String& getId() const { return id; }
    const juce::String& getKind() const { return kind; }
    bool isOnline() const { return node->isOnline(); }

    static juce::DynamicObject::Ptr makeMessage (const juce::String& type)
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("type", type);
        return o;
    }

    // to: an endpoint id, or "*" for everyone
    void send (juce::DynamicObject::Ptr msg, const juce::String& to = "*")
    {
        msg->setProperty ("from", id);
        msg->setProperty ("kind", kind);
        msg->setProperty ("to", to);
        msg->setProperty ("protocol", kProtocol);
        node->post (juce::JSON::toString (juce::var (msg.get()), true), this);
    }

    void deliver (const juce::var& msg)
    {
        const auto to = msg.getProperty ("to", "*").toString();
        if (msg.getProperty ("from", "").toString() == id) return;
        if (to != "*" && to != id) return;
        if ((int) msg.getProperty ("protocol", 0) != kProtocol) return;
        if (handler) handler (msg);
    }

private:
    juce::String kind, id;
    Handler handler;
    std::shared_ptr<Node> node;
};

inline void Node::deliverLocal (const juce::String& json, Endpoint* sender)
{
    const auto v = juce::JSON::parse (json);
    if (! v.isObject()) return;
    for (auto* e : juce::Array<Endpoint*> (locals))
        if (e != sender && locals.contains (e)) e->deliver (v);
}

//==============================================================================
// Level fingerprints: per 100 ms of wall-clock time, the input and output level of a
// plugin. Comparing one plugin's output with another's input tells The Master which
// plugins sit in its chain and in what order. Audio thread writes, message thread reads.
class EnvTap
{
public:
    static constexpr int kSlots = 64;

    // audio thread
    void add (const float* const* in, const float* const* out, int numCh, int n)
    {
        double si = 0, so = 0;
        for (int c = 0; c < numCh; ++c)
            for (int i = 0; i < n; ++i) { si += (double) in[c][i] * in[c][i]; so += (double) out[c][i] * out[c][i]; }
        const auto slot = (juce::int64) (juce::Time::getMillisecondCounterHiRes() / 100.0);
        if (slot != curSlot)
        {
            if (curSlot >= 0 && count > 0) store (curSlot, accIn / count, accOut / count);
            curSlot = slot; accIn = accOut = 0; count = 0;
        }
        accIn += si / juce::jmax (1, numCh); accOut += so / juce::jmax (1, numCh); count += n;
    }
    // shortcut when input and output buffers are the same channel arrays (in-place plugins)
    void addPowers (double inPow, double outPow, int n)
    {
        const auto slot = (juce::int64) (juce::Time::getMillisecondCounterHiRes() / 100.0);
        if (slot != curSlot)
        {
            if (curSlot >= 0 && count > 0) store (curSlot, accIn / count, accOut / count);
            curSlot = slot; accIn = accOut = 0; count = 0;
        }
        accIn += inPow * n; accOut += outPow * n; count += n;
    }

    // message thread: the last few seconds as { "slot": first, "in": [...], "out": [...] } (dB, -100 = silent)
    juce::var toVar (int numSlots = 50) const
    {
        const auto now = (juce::int64) (juce::Time::getMillisecondCounterHiRes() / 100.0) - 1;
        auto* o = new juce::DynamicObject();
        juce::Array<juce::var> a, b;
        const auto first = now - numSlots + 1;
        for (auto s = first; s <= now; ++s)
        {
            const int k = (int) (s % kSlots);
            const bool ok = stamp[(size_t) k].load() == s;
            a.add (ok ? (double) inDb[(size_t) k].load() : -100.0);
            b.add (ok ? (double) outDb[(size_t) k].load() : -100.0);
        }
        o->setProperty ("slot", (double) first);
        o->setProperty ("in", a);
        o->setProperty ("out", b);
        return juce::var (o);
    }

private:
    void store (juce::int64 slot, double pi, double po)
    {
        const int k = (int) (slot % kSlots);
        inDb[(size_t) k].store (pi > 1e-10 ? (float) (10.0 * std::log10 (pi)) : -100.0f);
        outDb[(size_t) k].store (po > 1e-10 ? (float) (10.0 * std::log10 (po)) : -100.0f);
        stamp[(size_t) k].store (slot);
    }
    juce::int64 curSlot = -1;
    double accIn = 0, accOut = 0;
    int count = 0;
    std::array<std::atomic<float>, kSlots> inDb {}, outDb {};
    std::array<std::atomic<juce::int64>, kSlots> stamp {};
};

// Does fingerprint a (e.g. a plugin's output) carry the same audio as b (e.g. The Master's
// input)? Returns the mean level difference in dB over the shared non-silent slots, or a
// large number if there isn't enough to compare.
inline float envDistance (const juce::var& a, const juce::String& aKey, const juce::var& b, const juce::String& bKey, int& shared)
{
    shared = 0;
    const auto* x = a.getProperty (aKey, {}).getArray();
    const auto* y = b.getProperty (bKey, {}).getArray();
    if (x == nullptr || y == nullptr) return 1e9f;
    const auto ax = (juce::int64) (double) a.getProperty ("slot", 0.0), by = (juce::int64) (double) b.getProperty ("slot", 0.0);
    double sum = 0, mean = 0;
    std::vector<double> d;
    for (int i = 0; i < x->size(); ++i)
    {
        const auto s = ax + i;
        const int j = (int) (s - by);
        if (j < 0 || j >= y->size()) continue;
        const double u = (*x)[i], v = (*y)[j];
        if (u < -70.0 || v < -70.0) continue;
        d.push_back (u - v);
    }
    shared = (int) d.size();
    if (shared < 8) return 1e9f;
    for (auto v : d) mean += v;
    mean /= (double) shared;
    for (auto v : d) sum += std::abs (v - mean);   // shape match; a constant offset (gain) is allowed
    return (float) (sum / (double) shared);
}

// Helpers for linked plugins: apply a "set" message to an APVTS, read params into a var.
template <typename APVTS>
inline juce::StringArray applySet (APVTS& apvts, const juce::var& params)
{
    juce::StringArray changed;
    if (auto* o = params.getDynamicObject())
        for (auto& p : o->getProperties())
            if (auto* rp = apvts.getParameter (p.name.toString()))
            {
                const float v = (float) (double) p.value;
                rp->beginChangeGesture();
                rp->setValueNotifyingHost (rp->convertTo0to1 (v));
                rp->endChangeGesture();
                changed.add (p.name.toString());
            }
    return changed;
}

template <typename APVTS>
inline juce::var paramsToVar (APVTS& apvts, const juce::StringArray& ids)
{
    auto* o = new juce::DynamicObject();
    for (auto& id : ids)
        if (auto* rp = apvts.getParameter (id)) o->setProperty (id, (double) rp->convertFrom0to1 (rp->getValue()));
    return juce::var (o);
}

//==============================================================================
// Everything a linked plugin (Dominatrix, Chop Shop, The Servant...) needs: answers The
// Master, reports its controls + level fingerprints 4 times a second, and applies the
// settings The Master sends (exactly as if the knobs were turned: the host sees it,
// undo and automation work).
template <typename APVTS>
class PluginLink : private juce::Timer
{
public:
    PluginLink (const juce::String& kind, const juce::String& displayName, const juce::String& version,
                APVTS& state, juce::StringArray paramIds)
        : apvts (state), ids (std::move (paramIds)), name (displayName), ver (version),
          endpoint (kind, [this] (const juce::var& m) { handle (m); })
    {
        startTimerHz (4);
    }
    ~PluginLink() override { stopTimer(); }

    EnvTap env;                                  // feed from processBlock
    std::function<juce::var()> extraStatus;      // optional extra fields (e.g. input peak)
    std::function<void (const juce::StringArray& changed)> onSetByMaster;
    std::function<void()> onBeforeSet;                        // e.g. so the plugin doesn't mark these as user overrides
    std::function<void (const juce::var& msg)> onOtherMessage; // anything that isn't hello / set
    std::function<void (juce::DynamicObject& params)> filterSet; // drop what the plugin keeps locked
    void sendStatusNow() { sendStatus ("*"); }                  // e.g. right after it finished something The Master waits for

    bool masterNearby() const { return lastMasterMs > 0 && juce::Time::getMillisecondCounter() - lastMasterMs < 3000; }
    juce::String getLastNote() const { return lastNote; }
    juce::uint32 getLastNoteTime() const { return lastNoteMs; }
    const juce::String& getId() const { return endpoint.getId(); }

private:
    void timerCallback() override { sendStatus ("*"); }

    void sendStatus (const juce::String& to)
    {
        auto m = Endpoint::makeMessage ("status");
        m->setProperty ("name", name);
        m->setProperty ("version", ver);
        m->setProperty ("params", paramsToVar (apvts, ids));
        m->setProperty ("env", env.toVar());
        if (extraStatus) m->setProperty ("extra", extraStatus());
        endpoint.send (m, to);
    }

    void handle (const juce::var& m)
    {
        const auto type = m.getProperty ("type", "").toString();
        const auto from = m.getProperty ("from", "").toString();
        if (m.getProperty ("kind", "").toString() == "master") lastMasterMs = juce::Time::getMillisecondCounter();
        if (type == "hello") sendStatus (from);
        else if (type == "set")
        {
            if (onBeforeSet) onBeforeSet();
            auto params = m.getProperty ("params", {});
            if (filterSet)
                if (auto* o = params.getDynamicObject())
                {
                    auto copy = o->clone();
                    filterSet (*copy);
                    params = juce::var (copy.release());
                }
            const auto changed = applySet (apvts, params);
            lastNote = m.getProperty ("note", "").toString();
            lastNoteMs = juce::Time::getMillisecondCounter();
            if (onSetByMaster) onSetByMaster (changed);
            auto r = Endpoint::makeMessage ("applied");
            r->setProperty ("params", paramsToVar (apvts, ids));
            r->setProperty ("changed", changed.joinIntoString (","));
            endpoint.send (r, from);
        }
        else if (onOtherMessage) onOtherMessage (m);
    }

    APVTS& apvts;
    juce::StringArray ids;
    juce::String name, ver, lastNote;
    juce::uint32 lastMasterMs = 0, lastNoteMs = 0;
    Endpoint endpoint;
};
} // namespace cdlink

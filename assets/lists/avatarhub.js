.pragma library

// Zalo10 "Lite" (L1 + L2): shared avatar bookkeeping for the Chats / Contacts / Groups tabs.
//
// Why this exists
// ---------------
// zService.downloadAvatar() answers synchronously (avatarReady) for every avatar that is already
// on disk. Each of the three tabs used to answer by scanning its WHOLE model with
// ArrayDataModel.value(i) (every call copies a row into a JS object). With N friends that is
// N requests x 3 tabs x up to N rows = O(N^2) work, all on the UI thread.
//
// What it does
// ------------
//  1. Row index (L2): threadId -> row, so avatarReady is an O(1) lookup instead of a scan.
//     A miss is answered instantly (group avatars never touch the friends tab, cover images never
//     touch the group tab, ...). A hit is verified against the row, so reordering the list
//     (new message moves a chat to the top) is safe: a stale hit just triggers one rebuild.
//  2. Request queue (L1 + L2): avatars are requested at a steady pace (MAX_PER_TICK every
//     TICK_MS, also for avatars that are already cached) and never more than MAX_INFLIGHT
//     network downloads at once, instead of everything in one blocking burst.
//
// This file is a .pragma library: ONE instance is shared by every QML file that imports it, so
// state is shared. It cannot see QML ids or zService; callers pass what it needs.

var TICK_MS       = 300;    // pacing interval of the owner's Timer
var MAX_PER_TICK  = 24;     // avatar requests handed to zService per tick
var MAX_INFLIGHT  = 6;      // simultaneous network downloads per list
var INFLIGHT_TTL  = 5000;   // ms before a request that never answered (failed download) is forgotten

// ---------------------------------------------------------------------------------------------
// Row index
// ---------------------------------------------------------------------------------------------

var indexes = {};

function rowKey(d) {
    if (!d) return "";
    return d.threadId || d.uid || "";
}

function getIndex(name) {
    var ix = indexes[name];
    if (!ix) { ix = { map: {}, valid: false, size: -1 }; indexes[name] = ix; }
    return ix;
}

// Call after the model's MEMBERSHIP changed (clear / append / remove). Reordering needs nothing.
function invalidate(name) {
    var ix = getIndex(name);
    ix.valid = false;
}

function rebuild(ix, model) {
    var map = {};
    var n = model.size();
    for (var k = 0; k < n; k++) {
        var key = rowKey(model.value(k));
        if (key.length > 0 && map[key] === undefined) map[key] = k;
    }
    ix.map = map;
    ix.size = n;
    ix.valid = true;
}

// Find the row of `id` in `model` (an ArrayDataModel: size(), value(i)).
// Returns { i: rowIndex, row: rowObject } or null. `row` is the copy already read for the
// verification, so the caller can modify it and model.replace(i, row) without reading it again.
function find(name, model, id) {
    if (!id || id.length === 0) return null;
    var ix = getIndex(name);
    if (ix.valid && ix.size !== model.size()) ix.valid = false;   // membership changed behind our back
    if (!ix.valid) rebuild(ix, model);

    var i = ix.map[id];
    if (i === undefined) return null;                              // authoritative miss, O(1)
    var row = model.value(i);
    if (rowKey(row) === id) return { i: i, row: row };             // verified hit

    // Rows were reordered since the index was built: rebuild once and answer again.
    rebuild(ix, model);
    i = ix.map[id];
    if (i === undefined) return null;
    row = model.value(i);
    return (rowKey(row) === id) ? { i: i, row: row } : null;
}

// ---------------------------------------------------------------------------------------------
// Paced request queue
// ---------------------------------------------------------------------------------------------

var queues = {};

function getQueue(name) {
    var q = queues[name];
    if (!q) { q = { items: [], pos: 0, queued: {}, inflight: {}, inflightCount: 0 }; queues[name] = q; }
    return q;
}

// Forget everything pending for this list (call before the list is rebuilt).
function reset(name) {
    queues[name] = { items: [], pos: 0, queued: {}, inflight: {}, inflightCount: 0 };
}

// Queue one avatar request. Duplicates of the same (id, url) that are still waiting are dropped.
function enqueue(name, id, url) {
    if (!id || !url || id.length === 0 || url.length === 0) return;
    var q = getQueue(name);
    var key = id + "\n" + url;
    if (q.queued[key]) return;
    q.queued[key] = true;
    q.items.push({ id: id, url: url });
}

// Call from the avatarReady handler: the download for `id` finished, free its slot.
function arrived(name, id) {
    var q = queues[name];
    if (!q) return;
    if (q.inflight[id] !== undefined) {
        delete q.inflight[id];
        q.inflightCount--;
    }
}

// Hand the next batch to zService. Returns how many requests are still waiting, so the owner's
// Timer can stop when it reaches 0. `svc` is zService (needs downloadAvatar(id, url)).
function drain(name, svc, now) {
    var q = getQueue(name);

    // Free slots held by downloads that never answered (network error emits no signal).
    if (q.inflightCount > 0) {
        for (var id in q.inflight) {
            if (now - q.inflight[id] > INFLIGHT_TTL) {
                delete q.inflight[id];
                q.inflightCount--;
            }
        }
    }

    var sent = 0;
    while (sent < MAX_PER_TICK && q.pos < q.items.length && q.inflightCount < MAX_INFLIGHT) {
        var it = q.items[q.pos++];
        delete q.queued[it.id + "\n" + it.url];
        // Mark BEFORE the call: a cached avatar answers synchronously and arrived() must find it.
        if (q.inflight[it.id] === undefined) {
            q.inflight[it.id] = now;
            q.inflightCount++;
        }
        svc.downloadAvatar(it.id, it.url);
        sent++;
    }

    var left = q.items.length - q.pos;
    if (left === 0) {            // compact: nothing waiting any more
        q.items = [];
        q.pos = 0;
    }
    return left;
}

// True while there is still something to hand out (owner uses it to keep its Timer running).
function pending(name) {
    var q = queues[name];
    return !!q && (q.items.length - q.pos) > 0;
}

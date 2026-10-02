import { useEffect, useRef, useState } from "react";
import {
  Users,
  ShieldCheck,
  Activity,
  Plus,
  Search,
  ArrowLeft,
  LogOut,
  Copy,
  Check,
  RefreshCw,
  X,
  Trash2,
  KeyRound,
  Monitor,
  ChevronRight,
  CircleHelp,
  LockKeyhole,
} from "lucide-react";
import type { Session, Peer, User, Detail, Head, Audit, Device } from "./types";

const key = (p: Peer) => `${p.kind}:${p.id}`;
const stamp = (n: number | null) =>
  n ? new Date(n * 1000).toLocaleString() : "Not yet";
function state(user: User) {
  const d = (user.devices || []).filter((x) => !x.revoked);
  if (!d.length) return "Not connected";
  if (d.every((x) => Date.now() / 1000 - x.last_seen > 120)) return "Offline";
  return d.every((x) => x.applied_revision >= user.revision)
    ? "Applied"
    : "Awaiting sync";
}
function Badge({ text }: { text: string }) {
  return (
    <span
      className={`badge ${text === "Applied" || text === "Active" ? "green" : text === "Awaiting sync" ? "amber" : "muted"}`}
    >
      {text}
    </span>
  );
}

export default function App() {
  const [session, setSession] = useState<Session | null>(null),
    [loading, setLoading] = useState(true),
    [page, setPage] = useState("users");
  const [users, setUsers] = useState<User[]>([]),
    [heads, setHeads] = useState<Head[]>([]),
    [events, setEvents] = useState<Audit[]>([]);
  const [detail, setDetail] = useState<Detail | null>(null),
    [peers, setPeers] = useState<Peer[]>([]),
    [base, setBase] = useState(0),
    [assigned, setAssigned] = useState<string[]>([]);
  const [query, setQuery] = useState(""),
    [error, setError] = useState(""),
    [notice, setNotice] = useState(""),
    [busy, setBusy] = useState(false),
    [code, setCode] = useState("");
  const [modal, setModal] = useState<
      null | "user" | "head" | "save" | "secret" | "revoke"
    >(null),
    [name, setName] = useState(""),
    [id, setId] = useState("");
  const [kind, setKind] = useState<Peer["kind"]>("user"),
    [peerId, setPeerId] = useState(""),
    [peerName, setPeerName] = useState(""),
    [peerQuery, setPeerQuery] = useState("");
  const [secret, setSecret] = useState({ title: "", value: "", hint: "" }),
    [copied, setCopied] = useState(false),
    [revoke, setRevoke] = useState<Device | null>(null);
  const alive = useRef(true),
    selected = useRef("");
  const dirty =
    !!detail && JSON.stringify(peers) !== JSON.stringify(detail.peers);
  const scopesDirty =
    !!detail &&
    JSON.stringify([...assigned].sort()) !==
      JSON.stringify([...detail.head_ids].sort());
  function clearPrivate() {
    setSession(null);
    setUsers([]);
    setHeads([]);
    setEvents([]);
    setDetail(null);
    setPeers([]);
    setAssigned([]);
    setModal(null);
    setSecret({ title: "", value: "", hint: "" });
    selected.current = "";
  }
  async function api<T>(
    path: string,
    body?: unknown,
    method = "GET",
  ): Promise<T> {
    const r = await fetch(path, {
      method,
      credentials: "same-origin",
      headers:
        body !== undefined
          ? {
              "Content-Type": "application/json",
              ...(session ? { "X-CSRF-Token": session.csrf } : {}),
            }
          : {},
      ...(body !== undefined ? { body: JSON.stringify(body) } : {}),
    });
    const data = await r
      .json()
      .catch(() => ({ detail: "The server returned an unreadable response." }));
    if (!r.ok) {
      if (r.status === 401 && session) clearPrivate();
      throw new Error(
        Array.isArray(data.detail)
          ? data.detail.map((x: { msg: string }) => x.msg).join("; ")
          : typeof data.detail === "string"
            ? data.detail
            : `Request failed (${r.status})`,
      );
    }
    return data;
  }
  async function run(fn: () => Promise<void>) {
    setBusy(true);
    setError("");
    setNotice("");
    try {
      await fn();
    } catch (e) {
      setError(
        e instanceof Error ? e.message : "Could not complete that action.",
      );
    } finally {
      setBusy(false);
    }
  }
  async function refresh() {
    const list = await api<User[]>("/api/users");
    if (alive.current) setUsers(list);
  }
  async function load(id: string) {
    const d = await api<Detail>(`/api/users/${id}`);
    selected.current = id;
    setDetail(d);
    setPeers(d.peers);
    setBase(d.revision);
    setAssigned(d.head_ids);
    setPeerQuery("");
  }
  function back() {
    if ((dirty || scopesDirty) && !window.confirm("Discard unsaved edits?"))
      return;
    selected.current = "";
    setDetail(null);
    setError("");
    setNotice("");
  }
  function navigate(p: string) {
    if ((dirty || scopesDirty) && !window.confirm("Discard unsaved edits?"))
      return;
    selected.current = "";
    setDetail(null);
    setPage(p);
    setError("");
    setNotice("");
  }
  useEffect(() => {
    alive.current = true;
    fetch("/api/session", { credentials: "same-origin" })
      .then(async (r) => {
        if (r.ok) {
          const s = await r.json();
          if (alive.current) setSession(s);
        } else if (r.status !== 401)
          throw new Error("Unable to connect to the board.");
      })
      .catch(() => setError("Unable to connect. Refresh to retry."))
      .finally(() => setLoading(false));
    return () => {
      alive.current = false;
    };
  }, []);
  useEffect(() => {
    if (!session) return;
    let cancelled = false;
    async function refreshAll() {
      try {
        const [u, h, a] = await Promise.all([
          api<User[]>("/api/users"),
          session?.role === "owner"
            ? api<Head[]>("/api/heads")
            : Promise.resolve([]),
          api<Audit[]>("/api/audit"),
        ]);
        if (!cancelled) {
          setUsers(u);
          setHeads(h);
          setEvents(a);
        }
      } catch (e) {
        if (!cancelled)
          setError(e instanceof Error ? e.message : "Unable to refresh");
      }
    }
    void refreshAll();
    const timer = setInterval(() => void refreshAll(), 15000);
    return () => {
      cancelled = true;
      clearInterval(timer);
    };
  }, [session?.id, session?.role, page]);
  useEffect(() => {
    if (!session || !detail) return;
    const user = detail.id;
    const timer = setInterval(async () => {
      try {
        const next = await api<Detail>(`/api/users/${user}`);
        if (selected.current === user)
          setDetail((old) =>
            old
              ? { ...old, devices: next.devices, updated_at: next.updated_at }
              : old,
          );
        if (next.revision !== base)
          setNotice(
            `The server is now on revision ${next.revision}. Reload the list before saving.`,
          );
      } catch (e) {
        if (selected.current === user) {
          setError(e instanceof Error ? e.message : "Unable to refresh");
        }
      }
    }, 15000);
    return () => clearInterval(timer);
  }, [session?.id, detail?.id, base]);
  async function signIn(e: React.FormEvent) {
    e.preventDefault();
    await run(async () => {
      const s = await api<Session>(
        "/api/auth/bootstrap",
        { code: code.trim() },
        "POST",
      );
      setCode("");
      setSession(s);
      setPage("users");
    });
  }
  function newIdentity(type: "user" | "head") {
    setName("");
    setId("");
    setError("");
    setModal(type);
  }
  async function create(e: React.FormEvent) {
    e.preventDefault();
    await run(async () => {
      const type = modal;
      await api(
        `/api/${type === "head" ? "heads" : "users"}`,
        { id: id.trim(), label: name.trim() },
        "POST",
      );
      setModal(null);
      if (type === "head") {
        setHeads(await api<Head[]>("/api/heads"));
        setNotice(
          "Head created. Issue a temporary access code to let them sign in.",
        );
      } else {
        await refresh();
        await load(id.trim());
        setNotice(
          "User created. Pair their updated Allowgram to preserve their initial list.",
        );
      }
    });
  }
  function addPeer(e: React.FormEvent) {
    e.preventDefault();
    const value = peerId.trim();
    if (!/^[1-9][0-9]{0,14}$/.test(value) || BigInt(value) > 281474976710655n) {
      setError("Enter a positive bare Telegram ID (without a -100 prefix).");
      return;
    }
    const entry: Peer = {
      kind,
      id: value,
      ...(peerName.trim() ? { label: peerName.trim() } : {}),
    };
    if (peers.some((p) => key(p) === key(entry))) {
      setError("That chat is already on the list.");
      return;
    }
    setPeers([...peers, entry].sort((a, b) => key(a).localeCompare(key(b))));
    setPeerId("");
    setPeerName("");
    setError("");
  }
  async function save() {
    if (!detail) return;
    await run(async () => {
      const result = await api<{ revision: number }>(
        `/api/users/${detail.id}/policy`,
        { expected_revision: base, peers },
        "PUT",
      );
      setModal(null);
      await load(detail.id);
      await refresh();
      setNotice(
        `Revision ${result.revision} saved. Awaiting device acknowledgement.`,
      );
    });
  }
  async function assign() {
    if (!detail) return;
    await run(async () => {
      await api(`/api/users/${detail.id}/heads`, { head_ids: assigned }, "PUT");
      setDetail({ ...detail, head_ids: assigned });
      setNotice("Head assignments updated.");
    });
  }
  async function invite() {
    if (!detail) return;
    await run(async () => {
      const result = await api<{ invitation: string }>(
        `/api/users/${detail.id}/invitations`,
        {},
        "POST",
      );
      setSecret({
        title: "Pair this user’s Allowgram",
        value: result.invitation,
        hint: "Single use • expires in 15 minutes. Send privately to this user. Requires an Allowgram build with managed-allowlist support. Older releases cannot pair.",
      });
      setCopied(false);
      setModal("secret");
    });
  }
  async function access(head: Head) {
    await run(async () => {
      const result = await api<{ code: string }>(
        `/api/heads/${head.id}/access-code`,
        {},
        "POST",
      );
      setSecret({
        title: `Access for ${head.label}`,
        value: result.code,
        hint: "One-time sign-in code • expires in 15 minutes. Share privately with this head. This is temporary capability-based login, not Telegram identity verification.",
      });
      setCopied(false);
      setModal("secret");
    });
  }
  const visibleUsers = users.filter((u) =>
    (u.label + " " + u.id).toLowerCase().includes(query.toLowerCase()),
  );
  const visiblePeers = peers.filter((p) =>
    (p.label + " " + p.id + " " + p.kind)
      .toLowerCase()
      .includes(peerQuery.toLowerCase()),
  );
  const added = peers.filter(
    (p) => !detail?.peers.some((o) => key(o) === key(p)),
  ).length;
  const removed = (detail?.peers || []).filter(
    (p) => !peers.some((o) => key(o) === key(p)),
  ).length;
  if (loading)
    return (
      <main className="loading">
        <span className="logo">A</span>
        <p>Opening Allowgram Control…</p>
      </main>
    );
  if (!session)
    return (
      <main className="login-page">
        <section className="login-intro">
          <div className="brand">
            <span className="logo">A</span>
            <strong>
              Allowgram<span>CONTROL</span>
            </strong>
          </div>
          <h1>
            The right chats.
            <br />
            <em>The right people.</em>
          </h1>
          <p>Manage allowed chats without interrupting a user’s session.</p>
          <div className="security-note">
            <ShieldCheck size={20} />
            <span>Scoped access. Signed policies. Clear sync status.</span>
          </div>
        </section>
        <section className="login-card">
          <div className="lock-icon">
            <LockKeyhole />
          </div>
          <h2>Welcome back</h2>
          <p>Sign in with your private one-time access code.</p>
          {error && (
            <div role="alert" className="alert error">
              {error}
            </div>
          )}
          <form onSubmit={signIn}>
            <label>
              Access code
              <input
                aria-label="Access code"
                type="password"
                autoComplete="off"
                value={code}
                onChange={(e) => setCode(e.target.value)}
                required
                minLength={20}
                maxLength={200}
              />
            </label>
            <button className="primary wide" disabled={busy}>
              {busy ? "Signing in…" : "Open control board"}
              <ChevronRight size={17} />
            </button>
          </form>
          <div className="callout">
            <CircleHelp size={17} />
            <span>
              Temporary Mac hosting. Telegram sign-in is not configured; use an
              owner-issued access code.
            </span>
          </div>
          <p className="tiny">
            No Telegram password, messages, or session files are requested.
          </p>
        </section>
      </main>
    );
  return (
    <div className="app">
      <aside className="sidebar">
        <div className="brand">
          <span className="logo">A</span>
          <strong>
            Allowgram<span>CONTROL</span>
          </strong>
        </div>
        <div className="nav-caption">WORKSPACE</div>
        <nav>
          <button
            className={page === "users" ? "active" : ""}
            onClick={() => navigate("users")}
          >
            <Users size={18} />
            Managed users<span className="nav-count">{users.length}</span>
          </button>
          {session.role === "owner" && (
            <button
              className={page === "heads" ? "active" : ""}
              onClick={() => navigate("heads")}
            >
              <ShieldCheck size={18} />
              Head accounts
            </button>
          )}
          <button
            className={page === "activity" ? "active" : ""}
            onClick={() => navigate("activity")}
          >
            <Activity size={18} />
            Activity
          </button>
        </nav>
        <div className="sidebar-bottom">
          <div className="host-note">
            <span className="dot" />
            Temporary Mac host
            <small>Requires the Mac and tunnel to stay online.</small>
          </div>
          <div className="identity">
            <span className="avatar">{session.label[0]}</span>
            <div>
              <strong>{session.label}</strong>
              <small>
                {session.role === "owner" ? "Workspace owner" : "Scoped head"}
              </small>
            </div>
            <button
              title="Sign out"
              aria-label="Sign out"
              onClick={() =>
                void run(async () => {
                  await api("/api/auth/logout", {}, "POST");
                  clearPrivate();
                })
              }
            >
              <LogOut size={17} />
            </button>
          </div>
        </div>
      </aside>
      <div className="workspace">
        <header className="topbar">
          <span>
            {page === "users"
              ? "People & permissions"
              : page === "heads"
                ? "Workspace access"
                : "Change history"}
          </span>
          <div className="private-label">
            <ShieldCheck size={15} />
            Private control board
          </div>
        </header>
        <main className="content">
          {error && (
            <div className="alert error" role="alert">
              {error}
              <button aria-label="Dismiss error" onClick={() => setError("")}>
                <X size={16} />
              </button>
            </div>
          )}
          {notice && (
            <div className="alert info" role="status">
              {notice}
              <button aria-label="Dismiss notice" onClick={() => setNotice("")}>
                <X size={16} />
              </button>
            </div>
          )}
          {page === "users" && !detail && (
            <>
              <div className="page-heading">
                <div>
                  <div className="eyebrow">ALLOWLIST MANAGEMENT</div>
                  <h1>Managed users</h1>
                  <p>
                    Choose who can access which chats. Keep their sessions
                    uninterrupted.
                  </p>
                </div>
                {session.role === "owner" && (
                  <button
                    className="primary"
                    onClick={() => newIdentity("user")}
                  >
                    <Plus size={17} />
                    Add user
                  </button>
                )}
              </div>
              <div className="stats">
                <div>
                  <span>Managed users</span>
                  <strong>{users.length}</strong>
                  <small>In your assigned scope</small>
                </div>
                <div>
                  <span>Awaiting sync</span>
                  <strong>
                    {users.filter((u) => state(u) === "Awaiting sync").length}
                  </strong>
                  <small>Saved, not yet acknowledged</small>
                </div>
                <div>
                  <span>Applied</span>
                  <strong>
                    {users.filter((u) => state(u) === "Applied").length}
                  </strong>
                  <small>All active devices acknowledged</small>
                </div>
              </div>
              <section className="panel">
                <div className="panel-toolbar">
                  <h2>People</h2>
                  <div className="search">
                    <Search size={16} />
                    <input
                      aria-label="Search users"
                      placeholder="Search by name or Telegram ID"
                      value={query}
                      onChange={(e) => setQuery(e.target.value)}
                    />
                  </div>
                </div>
                {!visibleUsers.length ? (
                  <div className="empty">
                    <span className="empty-icon">
                      <Users size={30} />
                    </span>
                    <h3>
                      {users.length
                        ? "No matching users"
                        : "Your workspace starts here"}
                    </h3>
                    <p>
                      {users.length
                        ? "Try a different name or Telegram ID."
                        : "Add a user, then pair their updated Allowgram. Their existing list is kept until you choose to change it."}
                    </p>
                    {!users.length && session.role === "owner" && (
                      <button
                        className="primary"
                        onClick={() => newIdentity("user")}
                      >
                        <Plus size={16} />
                        Add your first user
                      </button>
                    )}
                  </div>
                ) : (
                  <div className="table-wrap">
                    <table>
                      <thead>
                        <tr>
                          <th>User</th>
                          <th>Allowed chats</th>
                          <th>Revision</th>
                          <th>Sync status</th>
                          <th />
                        </tr>
                      </thead>
                      <tbody>
                        {visibleUsers.map((u) => (
                          <tr
                            key={u.id}
                            onClick={() => void run(() => load(u.id))}
                            className="clickable"
                          >
                            <td>
                              <div className="person">
                                <span className="avatar">{u.label[0]}</span>
                                <div>
                                  <button
                                    className="text-button"
                                    onClick={(e) => {
                                      e.stopPropagation();
                                      void run(() => load(u.id));
                                    }}
                                  >
                                    {u.label}
                                  </button>
                                  <small>{u.id}</small>
                                </div>
                              </div>
                            </td>
                            <td>{u.peer_count}</td>
                            <td>
                              {u.revision ? `v${u.revision}` : "Initial setup"}
                            </td>
                            <td>
                              <Badge text={state(u)} />
                            </td>
                            <td>
                              <ChevronRight size={17} />
                            </td>
                          </tr>
                        ))}
                      </tbody>
                    </table>
                  </div>
                )}
              </section>
              <div className="bottom-note">
                <ShieldCheck size={16} />
                The board manages Allowgram permissions only. It does not read
                Telegram messages.
              </div>
            </>
          )}
          {page === "users" && detail && (
            <>
              <button className="back" onClick={back}>
                <ArrowLeft size={16} />
                Managed users
              </button>
              <div className="page-heading">
                <div>
                  <div className="eyebrow">USER PERMISSIONS</div>
                  <h1>{detail.label}</h1>
                  <p>
                    Telegram ID <code>{detail.id}</code> · Revision{" "}
                    {base || "not initialized"}
                  </p>
                </div>
                <div className="actions">
                  <Badge text={state(detail)} />
                  <button
                    className="secondary"
                    disabled={busy}
                    onClick={() => void invite()}
                  >
                    <KeyRound size={16} />
                    Pair device
                  </button>
                </div>
              </div>
              <div className="detail-grid">
                <section className="panel">
                  <div className="panel-toolbar">
                    <h2>
                      Allowed chats{" "}
                      <span className="count">{peers.length}</span>
                    </h2>
                    <button
                      title="Reload saved list"
                      aria-label="Reload saved list"
                      className="icon-button"
                      onClick={() => {
                        if (
                          !dirty ||
                          window.confirm(
                            "Discard edits and reload the saved list?",
                          )
                        )
                          void run(() => load(detail.id));
                      }}
                    >
                      <RefreshCw size={17} />
                    </button>
                  </div>
                  <div className="editor-body">
                    <div className="search">
                      <Search size={16} />
                      <input
                        aria-label="Search allowed chats"
                        placeholder="Find an allowed chat"
                        value={peerQuery}
                        onChange={(e) => setPeerQuery(e.target.value)}
                      />
                    </div>
                    <div className="peers">
                      {visiblePeers.map((p) => (
                        <div className="peer" key={key(p)}>
                          <span className="peer-icon">
                            {p.kind === "user" ? (
                              <Users size={16} />
                            ) : (
                              <span>#</span>
                            )}
                          </span>
                          <div>
                            <strong>
                              {p.label ||
                                `${p.kind === "user" ? "Contact" : p.kind === "chat" ? "Group" : "Channel"} ${p.id}`}
                            </strong>
                            <small>
                              {p.kind} · {p.id}
                            </small>
                          </div>
                          <button
                            className="icon-button danger"
                            aria-label={`Remove ${p.label || p.id}`}
                            onClick={() =>
                              setPeers(peers.filter((x) => key(x) !== key(p)))
                            }
                          >
                            <Trash2 size={16} />
                          </button>
                        </div>
                      ))}
                      {!visiblePeers.length && (
                        <p className="quiet">
                          {peers.length
                            ? "No matching chats."
                            : detail.revision
                              ? "This managed list allows no chats."
                              : "Not initialized. Pair a device to import its existing allowed list, or define the list here."}
                        </p>
                      )}
                    </div>
                    <form className="add-peer" onSubmit={addPeer}>
                      <h3>Add a permitted chat</h3>
                      <div className="form-row">
                        <label>
                          Type
                          <select
                            aria-label="Chat type"
                            value={kind}
                            onChange={(e) =>
                              setKind(e.target.value as Peer["kind"])
                            }
                          >
                            <option value="user">Contact</option>
                            <option value="chat">Group</option>
                            <option value="channel">
                              Channel / supergroup
                            </option>
                          </select>
                        </label>
                        <label>
                          Telegram ID
                          <input
                            aria-label="Chat Telegram ID"
                            inputMode="numeric"
                            value={peerId}
                            onChange={(e) => setPeerId(e.target.value)}
                            placeholder="Bare numeric ID"
                            required
                            maxLength={15}
                          />
                        </label>
                      </div>
                      <label>
                        Display name <span className="optional">optional</span>
                        <input
                          aria-label="Chat display name"
                          value={peerName}
                          onChange={(e) => setPeerName(e.target.value)}
                          placeholder="e.g. Operations team"
                          maxLength={128}
                        />
                      </label>
                      <button className="secondary" type="submit">
                        <Plus size={16} />
                        Add to list
                      </button>
                      <p className="tiny">
                        Use bare IDs, without the -100 prefix. Allowing a chat
                        does not join it on the user’s behalf.
                      </p>
                    </form>
                  </div>
                  <div className="savebar">
                    <span>
                      {dirty ? "Unsaved changes" : "Saved list"}
                      {dirty && (
                        <small>
                          {added} added · {removed} removed
                        </small>
                      )}
                    </span>
                    <button
                      className="primary"
                      disabled={busy || (!dirty && detail.revision !== 0)}
                      onClick={() => setModal("save")}
                    >
                      Save changes
                    </button>
                  </div>
                </section>
                <div className="detail-side">
                  <section className="panel compact">
                    <h2>
                      <Monitor size={17} />
                      Connected devices
                    </h2>
                    {!detail.devices.length ? (
                      <p className="quiet">
                        No paired devices yet. An Allowgram build with
                        managed-allowlist support is required to pair.
                      </p>
                    ) : (
                      detail.devices.map((d) => (
                        <div className="device" key={d.id}>
                          <strong>{d.device_name}</strong>
                          <Badge
                            text={
                              d.revoked
                                ? "Revoked"
                                : Date.now() / 1000 - d.last_seen > 120
                                  ? "Offline"
                                  : d.applied_revision >= base
                                    ? "Applied"
                                    : "Awaiting sync"
                            }
                          />
                          <dl>
                            <dt>Applied revision</dt>
                            <dd>{d.applied_revision || "None"}</dd>
                            <dt>Last seen</dt>
                            <dd>{stamp(d.last_seen)}</dd>
                            <dt>App version</dt>
                            <dd>{d.client_version}</dd>
                          </dl>
                          {!d.revoked && (
                            <button
                              className="text-button danger"
                              onClick={() => {
                                setRevoke(d);
                                setModal("revoke");
                              }}
                            >
                              Revoke device access
                            </button>
                          )}
                        </div>
                      ))
                    )}
                  </section>
                  {session.role === "owner" && (
                    <section className="panel compact">
                      <h2>
                        <ShieldCheck size={17} />
                        Assigned heads
                      </h2>
                      <p className="tiny">You always retain owner access.</p>
                      {heads
                        .filter((h) => h.role === "head" && h.active)
                        .map((h) => (
                          <label className="checkbox" key={h.id}>
                            <input
                              type="checkbox"
                              checked={assigned.includes(h.id)}
                              onChange={(e) =>
                                setAssigned(
                                  e.target.checked
                                    ? [...assigned, h.id]
                                    : assigned.filter((x) => x !== h.id),
                                )
                              }
                            />
                            <span>
                              {h.label}
                              <small>{h.id}</small>
                            </span>
                          </label>
                        ))}
                      {!heads.some((h) => h.role === "head" && h.active) && (
                        <p className="quiet">
                          Create a head account to delegate management.
                        </p>
                      )}
                      <button
                        className="secondary wide"
                        disabled={!scopesDirty || busy}
                        onClick={() => void assign()}
                      >
                        Save assignments
                      </button>
                    </section>
                  )}
                  <div className="callout">
                    <CircleHelp size={18} />
                    <span>
                      Changes apply without signing the user out. Offline
                      clients retain their last verified list and catch up when
                      they reconnect.
                    </span>
                  </div>
                </div>
              </div>
            </>
          )}
          {page === "heads" && (
            <>
              <div className="page-heading">
                <div>
                  <div className="eyebrow">DELEGATED ACCESS</div>
                  <h1>Head accounts</h1>
                  <p>
                    Give trusted people access to the users they manage—nothing
                    more.
                  </p>
                </div>
                <button className="primary" onClick={() => newIdentity("head")}>
                  <Plus size={17} />
                  Add head
                </button>
              </div>
              <section className="panel">
                <div className="table-wrap">
                  <table>
                    <thead>
                      <tr>
                        <th>Account</th>
                        <th>Role</th>
                        <th>Status</th>
                        <th>Access</th>
                      </tr>
                    </thead>
                    <tbody>
                      {heads.map((h) => (
                        <tr key={h.id}>
                          <td>
                            <div className="person">
                              <span className="avatar">{h.label[0]}</span>
                              <div>
                                <strong>{h.label}</strong>
                                <small>{h.id}</small>
                              </div>
                            </div>
                          </td>
                          <td>
                            {h.role === "owner" ? "Owner" : "Scoped head"}
                          </td>
                          <td>
                            <Badge text={h.active ? "Active" : "Disabled"} />
                          </td>
                          <td>
                            <div className="actions">
                              {!!h.active && (
                                <button
                                  className="secondary"
                                  disabled={busy}
                                  onClick={() => void access(h)}
                                >
                                  <KeyRound size={15} />
                                  Sign-in code
                                </button>
                              )}
                              {h.role !== "owner" && (
                                <button
                                  className="text-button danger"
                                  disabled={busy}
                                  onClick={() => {
                                    if (
                                      window.confirm(
                                        `${h.active ? "Disable" : "Enable"} ${h.label}?`,
                                      )
                                    )
                                      void run(async () => {
                                        await api(
                                          `/api/heads/${h.id}`,
                                          { label: h.label, active: !h.active },
                                          "PATCH",
                                        );
                                        setHeads(
                                          await api<Head[]>("/api/heads"),
                                        );
                                      });
                                  }}
                                >
                                  {h.active ? "Disable" : "Enable"}
                                </button>
                              )}
                            </div>
                          </td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </section>
              <div className="callout">
                <CircleHelp size={18} />
                <span>
                  Temporary access codes identify the head chosen by the owner.
                  Telegram sign-in requires a separate bot/OIDC configuration;
                  it is not active on this temporary host.
                </span>
              </div>
            </>
          )}
          {page === "activity" && (
            <>
              <div className="page-heading">
                <div>
                  <div className="eyebrow">ACCOUNTABILITY</div>
                  <h1>Activity</h1>
                  <p>
                    A record of policy changes, access grants, and device
                    acknowledgements.
                  </p>
                </div>
                <button
                  className="secondary"
                  onClick={() =>
                    void run(async () =>
                      setEvents(await api<Audit[]>("/api/audit")),
                    )
                  }
                >
                  <RefreshCw size={16} />
                  Refresh
                </button>
              </div>
              <section className="panel">
                {!events.length ? (
                  <div className="empty">
                    <Activity size={28} />
                    <h3>No activity yet</h3>
                    <p>Changes within your scope will appear here.</p>
                  </div>
                ) : (
                  <div className="audit-list">
                    {events.map((e) => (
                      <div className="audit-event" key={e.id}>
                        <span className="audit-dot" />
                        <div>
                          <strong>
                            {e.action
                              .replaceAll(".", " · ")
                              .replaceAll("-", " ")}
                          </strong>
                          <small>
                            {e.user_id ? `User ${e.user_id} · ` : ""}
                            {e.actor_id.startsWith("device:")
                              ? "Device acknowledgement"
                              : `By ${e.actor_id}`}
                          </small>
                          <p>
                            {Object.entries(e.detail)
                              .map(
                                ([k, v]) =>
                                  `${k.replaceAll("_", " ")}: ${Array.isArray(v) ? v.join(", ") : String(v)}`,
                              )
                              .join(" · ")}
                          </p>
                        </div>
                        <time>{stamp(e.at)}</time>
                      </div>
                    ))}
                  </div>
                )}
              </section>
            </>
          )}
        </main>
        <footer className="footer">
          Allowgram Control{" "}
          <span>
            Temporary host · Signed policies · Device acknowledgements
          </span>
        </footer>
      </div>
      {modal && (
        <div className="modal-backdrop">
          <section
            className="modal"
            role="dialog"
            aria-modal="true"
            aria-label={
              modal === "secret"
                ? secret.title
                : modal === "user"
                  ? "Add user"
                  : modal === "head"
                    ? "Add head"
                    : modal === "save"
                      ? "Confirm allowlist changes"
                      : "Revoke device"
            }
          >
            <button
              className="modal-close icon-button"
              aria-label="Close dialog"
              disabled={busy}
              onClick={() => {
                setModal(null);
                setSecret({ title: "", value: "", hint: "" });
              }}
            >
              <X size={20} />
            </button>
            {error && (
              <div role="alert" className="alert error">
                {error}
              </div>
            )}
            {(modal === "user" || modal === "head") && (
              <>
                <h2>
                  {modal === "user"
                    ? "Add a managed user"
                    : "Add a head account"}
                </h2>
                <p>
                  {modal === "user"
                    ? "Use the numeric Telegram ID shown in the user’s Allowgram. Pairing does not transfer their Telegram session."
                    : "This head only gets access to users you explicitly assign."}
                </p>
                <form onSubmit={create}>
                  <label>
                    Display name
                    <input
                      autoFocus
                      required
                      maxLength={128}
                      value={name}
                      onChange={(e) => setName(e.target.value)}
                    />
                  </label>
                  <label>
                    Telegram user ID
                    <input
                      inputMode="numeric"
                      required
                      pattern="[1-9][0-9]{0,14}"
                      maxLength={15}
                      value={id}
                      onChange={(e) => setId(e.target.value)}
                    />
                  </label>
                  <button className="primary wide" disabled={busy}>
                    {busy
                      ? "Creating…"
                      : modal === "user"
                        ? "Create user"
                        : "Create head"}
                  </button>
                </form>
              </>
            )}
            {modal === "save" && (
              <>
                <h2>Apply these list changes?</h2>
                <p>
                  {added} added · {removed} removed · {peers.length} allowed
                  chats after saving.
                </p>
                {!peers.length && (
                  <div className="alert error">
                    This is an empty managed list. No chats will be allowed by
                    this policy. The user remains signed in.
                  </div>
                )}
                <p>
                  Online clients apply the signed policy on their next sync.
                  Offline clients keep their previous list until they reconnect.
                </p>
                <button
                  className="primary wide"
                  disabled={busy}
                  onClick={() => void save()}
                >
                  {busy ? "Saving…" : "Confirm and save"}
                </button>
              </>
            )}
            {modal === "secret" && (
              <>
                <div className="lock-icon">
                  <KeyRound />
                </div>
                <h2>{secret.title}</h2>
                <p>{secret.hint}</p>
                <textarea
                  aria-label="Private code"
                  className="secret"
                  readOnly
                  value={secret.value}
                  rows={4}
                />
                <button
                  className="primary wide"
                  onClick={() =>
                    void navigator.clipboard
                      .writeText(secret.value)
                      .then(() => setCopied(true))
                      .catch(() =>
                        setError(
                          "Clipboard unavailable. Select and copy the code manually.",
                        ),
                      )
                  }
                >
                  {copied ? <Check size={16} /> : <Copy size={16} />}{" "}
                  {copied ? "Copied" : "Copy private code"}
                </button>
                <p className="tiny">
                  Shown once. Closing this dialog removes it from the page.
                </p>
              </>
            )}
            {modal === "revoke" && revoke && detail && (
              <>
                <h2>Stop this device’s sync?</h2>
                <p>
                  {revoke.device_name} will no longer receive policies. Its last
                  downloaded restrictions remain in place. This does not
                  remotely sign it out or unlock chats.
                </p>
                <button
                  className="primary wide"
                  disabled={busy}
                  onClick={() =>
                    void run(async () => {
                      await api(
                        `/api/users/${detail.id}/devices/${revoke.id}/revoke`,
                        {},
                        "POST",
                      );
                      setModal(null);
                      await load(detail.id);
                      setNotice("Device sync access revoked.");
                    })
                  }
                >
                  Revoke device
                </button>
              </>
            )}
          </section>
        </div>
      )}
    </div>
  );
}

import { useState } from 'react';

export type Connection = {
  id: string; telegram_user_id: string; device_name: string; client_version: string;
  fingerprint: string; created_at: number; identity_verified: false;
};

export default function Connections({ rows, busy, approve, reject }: {
  rows: Connection[]; busy: boolean;
  approve: (row: Connection) => void; reject: (row: Connection) => void;
}) {
  const [confirmed, setConfirmed] = useState<Record<string, boolean>>({});
  return <section className="panel connections" aria-label="New connections">
    <div className="panel-toolbar"><h2>New connections · {rows.length}</h2></div>
    <p>Users appear here after accepting the connection prompt. IDs are self-reported, not verified Telegram identities. Confirm the code on the user's device before granting access.</p>
    {!rows.length && <p>No pending connections. This list refreshes automatically.</p>}
    {rows.map(row => <article key={row.id} data-testid={`connection-${row.id}`} className="connection-card">
      <h3>Telegram ID {row.telegram_user_id} <span className="badge amber">Unverified · pending approval</span></h3>
      <p>{row.device_name} · {row.client_version} · {new Date(row.created_at * 1000).toLocaleString()}</p>
      <p>Device verification code: <code>{row.fingerprint}</code></p>
      <label className="connection-confirm"><input type="checkbox" checked={!!confirmed[row.id]} onChange={e => setConfirmed({...confirmed, [row.id]:e.target.checked})} />I verified this code on the intended user's device.</label>
      <div className="connection-actions">
        <button className="primary" disabled={busy || !confirmed[row.id]} onClick={() => approve(row)}>Approve connection</button>
        <button className="secondary" disabled={busy} onClick={() => { if (window.confirm('Reject this connection? It will not receive any managed settings.')) reject(row); }}>Reject connection</button>
      </div>
    </article>)}
  </section>;
}

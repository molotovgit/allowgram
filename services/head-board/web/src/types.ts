export type Session = {
  id: string;
  label: string;
  role: "owner" | "head";
  csrf: string;
};
export type Peer = {
  kind: "user" | "chat" | "channel";
  id: string;
  label?: string;
};
export type Device = {
  id: string;
  device_name: string;
  client_version: string;
  revoked: number;
  last_seen: number;
  applied_revision: number;
  applied_at: number | null;
};
export type User = {
  id: string;
  label: string;
  revision: number;
  updated_at: number;
  peer_count: number;
  devices?: Device[];
};
export type Detail = User & {
  peers: Peer[];
  head_ids: string[];
  devices: Device[];
};
export type Head = {
  id: string;
  label: string;
  role: "owner" | "head";
  active: number;
};
export type Audit = {
  id: number;
  at: number;
  actor_id: string;
  user_id: string | null;
  action: string;
  detail: Record<string, unknown>;
};

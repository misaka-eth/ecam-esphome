// Live state of the ESP's entities from ESPHome web_server (v3): the /events stream sends one "state"
// event per entity (with full details on connect), commands are POSTs to /<domain>/<name>/<method>.
import { useSyncExternalStore } from 'react';

export interface Entity {
  id: string; // "<domain>/<name>"
  domain?: string;
  name?: string;
  value?: unknown;
  state?: string;
  option?: string[];
  min_value?: string;
  max_value?: string;
  step?: string;
  uom?: string;
  max_length?: number;
  entity_category?: number;
}

type Store = { entities: Record<string, Entity>; connected: boolean; title: string };

let store: Store = { entities: {}, connected: false, title: '' };
const listeners = new Set<() => void>();

function emit(next: Partial<Store>) {
  store = { ...store, ...next };
  listeners.forEach((l) => l());
}

function connect() {
  const es = new EventSource('/events');
  es.addEventListener('open', () => emit({ connected: true }));
  es.addEventListener('error', () => emit({ connected: false }));
  es.addEventListener('ping', (e) => {
    try {
      const d = JSON.parse((e as MessageEvent).data);
      if (d.title && d.title !== store.title) emit({ title: d.title });
    } catch {
      /* ping without JSON */
    }
  });
  es.addEventListener('state', (e) => {
    const d = JSON.parse((e as MessageEvent).data) as Entity;
    emit({ entities: { ...store.entities, [d.id]: { ...store.entities[d.id], ...d } } });
  });
}

connect();

export function useStore(): Store {
  return useSyncExternalStore(
    (l) => {
      listeners.add(l);
      return () => listeners.delete(l);
    },
    () => store,
  );
}

export function useEntity(domain: string, name: string): Entity | undefined {
  return useStore().entities[`${domain}/${name}`];
}

export async function call(domain: string, name: string, method: string, params: Record<string, string | number> = {}) {
  const q = new URLSearchParams(Object.entries(params).map(([k, v]) => [k, String(v)])).toString();
  const res = await fetch(`/${domain}/${encodeURIComponent(name)}/${method}${q ? `?${q}` : ''}`, { method: 'POST' });
  if (!res.ok) throw new Error(`${domain}/${name}/${method}: ${res.status}`);
}

// Helpers for reading entity values.
export function num(e?: Entity): number | undefined {
  const v = e?.value;
  return typeof v === 'number' && Number.isFinite(v) ? v : undefined;
}

export function str(e?: Entity): string | undefined {
  if (!e) return undefined;
  if (typeof e.value === 'string' && e.value !== '') return e.value;
  if (e.state && e.state !== 'NA' && e.state !== '') return e.state;
  return undefined;
}

export function on(e?: Entity): boolean | undefined {
  return typeof e?.value === 'boolean' ? e.value : undefined;
}

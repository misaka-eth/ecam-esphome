import { ReactNode, useEffect, useState } from 'react';
import { call, Entity, num, on, str, useEntity } from './esp';
import { t } from './i18n';

// ---- icons (paths after Lucide, ISC licence)

const paths: Record<string, ReactNode> = {
  cup: (
    <>
      <path d="M17 8h1a4 4 0 1 1 0 8h-1" />
      <path d="M3 8h14v9a4 4 0 0 1-4 4H7a4 4 0 0 1-4-4Z" />
      <path d="M6 2v2M10 2v2M14 2v2" />
    </>
  ),
  recipe: (
    <>
      <path d="M4 21v-7M4 10V3M12 21v-9M12 8V3M20 21v-5M20 12V3" />
      <path d="M1 14h6M9 8h6M17 16h6" />
    </>
  ),
  bean: (
    <>
      <ellipse cx="12" cy="12" rx="7" ry="9.5" transform="rotate(35 12 12)" />
      <path d="M8.5 18.5c2-3 1-5 3.5-7s2.5-4 3.5-6.5" />
    </>
  ),
  settings: (
    <>
      <path d="M12.2 2h-.4a2 2 0 0 0-2 2v.2a2 2 0 0 1-1 1.7l-.4.3a2 2 0 0 1-2 0l-.2-.1a2 2 0 0 0-2.7.7l-.2.4a2 2 0 0 0 .7 2.7l.2.1a2 2 0 0 1 1 1.7v.6a2 2 0 0 1-1 1.7l-.2.1a2 2 0 0 0-.7 2.7l.2.4a2 2 0 0 0 2.7.7l.2-.1a2 2 0 0 1 2 0l.4.3a2 2 0 0 1 1 1.7v.2a2 2 0 0 0 2 2h.4a2 2 0 0 0 2-2v-.2a2 2 0 0 1 1-1.7l.4-.3a2 2 0 0 1 2 0l.2.1a2 2 0 0 0 2.7-.7l.2-.4a2 2 0 0 0-.7-2.7l-.2-.1a2 2 0 0 1-1-1.7v-.6a2 2 0 0 1 1-1.7l.2-.1a2 2 0 0 0 .7-2.7l-.2-.4a2 2 0 0 0-2.7-.7l-.2.1a2 2 0 0 1-2 0l-.4-.3a2 2 0 0 1-1-1.7V4a2 2 0 0 0-2-2z" />
      <circle cx="12" cy="12" r="3" />
    </>
  ),
  chart: <path d="M3 3v18h18M7 16v-4M12 16V8M17 16v-7" />,
  wrench: (
    <path d="M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.8-3.8a6 6 0 0 1-7.9 7.9l-6.9 6.9a2.1 2.1 0 0 1-3-3l6.9-6.9a6 6 0 0 1 7.9-7.9z" />
  ),
  chip: (
    <>
      <rect x="5" y="5" width="14" height="14" rx="2" />
      <path d="M9 9h6v6H9zM9 2v3M15 2v3M9 19v3M15 19v3M2 9h3M2 15h3M19 9h3M19 15h3" />
    </>
  ),
  power: <path d="M12 2v10M18.4 6.6a9 9 0 1 1-12.8 0" />,
  stop: <rect x="6" y="6" width="12" height="12" rx="2" />,
  play: <path d="M6 3l14 9-14 9z" />,
  save: (
    <>
      <path d="M15.2 3a2 2 0 0 1 1.4.6l3.8 3.8a2 2 0 0 1 .6 1.4V19a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z" />
      <path d="M17 21v-7a1 1 0 0 0-1-1H8a1 1 0 0 0-1 1v7M7 3v4a1 1 0 0 0 1 1h7" />
    </>
  ),
  undo: <path d="M3 7v6h6M21 17a9 9 0 0 0-15-6.7L3 13" />,
  trash: <path d="M3 6h18M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2" />,
  drop: <path d="M12 22a7 7 0 0 0 7-7c0-2-1-3.9-3-5.5s-3.5-4-4-6.5c-.5 2.5-2 4.9-4 6.5S5 13 5 15a7 7 0 0 0 7 7z" />,
  alert: (
    <>
      <path d="M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z" />
      <path d="M12 9v4M12 17h.01" />
    </>
  ),
  check: <path d="M20 6 9 17l-5-5" />,
  restart: <path d="M3 12a9 9 0 1 0 9-9 9.8 9.8 0 0 0-6.7 2.7L3 8M3 3v5h5" />,
};

export function Icon({ name, className = 'h-5 w-5' }: { name: string; className?: string }) {
  return (
    <svg
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth={1.8}
      strokeLinecap="round"
      strokeLinejoin="round"
      className={className}
      aria-hidden
    >
      {paths[name]}
    </svg>
  );
}

// ---- layout

export function Card({ title, icon, right, children, className = '' }: {
  title?: string; icon?: string; right?: ReactNode; children: ReactNode; className?: string;
}) {
  return (
    <section
      className={`rounded-2xl border border-navy-700/70 bg-gradient-to-b from-navy-850 to-navy-900 p-5 shadow-xl shadow-black/20 ${className}`}
    >
      {title && (
        <header className="mb-4 flex items-center gap-2">
          {icon && <Icon name={icon} className="h-5 w-5 text-accent" />}
          <h2 className="text-sm font-semibold uppercase tracking-wider text-ink-muted">{title}</h2>
          <div className="ml-auto">{right}</div>
        </header>
      )}
      {children}
    </section>
  );
}

export function Row({ label, children, hint }: { label: string; children: ReactNode; hint?: string }) {
  return (
    <div className="flex min-h-[44px] items-center justify-between gap-4 py-1.5">
      <div>
        <div className="text-[15px] text-ink">{label}</div>
        {hint && <div className="text-xs text-ink-dim">{hint}</div>}
      </div>
      <div className="flex shrink-0 items-center">{children}</div>
    </div>
  );
}

export function Pill({ tone = 'blue', children }: { tone?: 'blue' | 'green' | 'amber' | 'red' | 'grey'; children: ReactNode }) {
  const tones = {
    blue: 'bg-accent/15 text-accent-glow ring-accent/30',
    green: 'bg-emerald-400/10 text-emerald-300 ring-emerald-400/30',
    amber: 'bg-crema/10 text-crema ring-crema/30',
    red: 'bg-rose-500/10 text-rose-300 ring-rose-400/30',
    grey: 'bg-navy-700/50 text-ink-muted ring-navy-600',
  };
  return <span className={`inline-flex items-center gap-1.5 rounded-full px-3 py-1 text-xs font-medium ring-1 ${tones[tone]}`}>{children}</span>;
}

// ---- controls

type Variant = 'primary' | 'ghost' | 'danger' | 'crema';

export function Button({ children, onClick, variant = 'ghost', icon, disabled, className = '' }: {
  children: ReactNode; onClick?: () => void; variant?: Variant; icon?: string; disabled?: boolean; className?: string;
}) {
  const styles: Record<Variant, string> = {
    primary: 'bg-accent text-navy-950 hover:bg-accent-glow shadow-lg shadow-accent/20',
    crema: 'bg-crema text-navy-950 hover:brightness-110 shadow-lg shadow-crema/20',
    ghost: 'bg-navy-800 text-ink hover:bg-navy-700 ring-1 ring-navy-700',
    danger: 'bg-rose-500/15 text-rose-300 hover:bg-rose-500/25 ring-1 ring-rose-400/30',
  };
  return (
    <button
      type="button"
      disabled={disabled}
      onClick={onClick}
      className={`inline-flex min-h-[44px] items-center justify-center gap-2 rounded-xl px-4 text-sm font-semibold transition active:scale-[0.97] disabled:pointer-events-none disabled:opacity-40 ${styles[variant]} ${className}`}
    >
      {icon && <Icon name={icon} className="h-[18px] w-[18px]" />}
      {children}
    </button>
  );
}

// A button that asks once more before it acts.
export function ConfirmButton({ children, onConfirm, ...rest }: {
  children: ReactNode; onConfirm: () => void; variant?: Variant; icon?: string; disabled?: boolean; className?: string;
}) {
  const [armed, setArmed] = useState(false);
  useEffect(() => {
    if (!armed) return;
    const id = setTimeout(() => setArmed(false), 3000);
    return () => clearTimeout(id);
  }, [armed]);
  return (
    <Button
      {...rest}
      onClick={() => {
        if (armed) {
          setArmed(false);
          onConfirm();
        } else setArmed(true);
      }}
    >
      {armed ? t('Really?') : children}
    </Button>
  );
}

export function Toggle({ checked, onChange, disabled }: { checked?: boolean; onChange: (v: boolean) => void; disabled?: boolean }) {
  return (
    <button
      type="button"
      role="switch"
      aria-checked={!!checked}
      disabled={disabled || checked === undefined}
      onClick={() => onChange(!checked)}
      className={`relative h-7 w-12 rounded-full transition disabled:opacity-40 ${checked ? 'bg-accent' : 'bg-navy-700'}`}
    >
      <span className={`absolute top-1 h-5 w-5 rounded-full bg-white shadow transition-all ${checked ? 'left-6' : 'left-1'}`} />
    </button>
  );
}

export function Segmented({ options, value, onChange, label = t }: {
  options: string[]; value?: string; onChange: (v: string) => void; label?: (s: string) => string;
}) {
  return (
    <div className="flex flex-wrap gap-1.5">
      {options.map((o) => (
        <button
          key={o}
          type="button"
          onClick={() => onChange(o)}
          className={`min-h-[36px] rounded-lg px-3 text-sm transition ${
            o === value ? 'bg-accent text-navy-950 font-semibold' : 'bg-navy-800 text-ink-muted ring-1 ring-navy-700 hover:text-ink'
          }`}
        >
          {label(o)}
        </button>
      ))}
    </div>
  );
}

export function Dropdown({ options, value, onChange, label = t, className = '' }: {
  options: string[]; value?: string; onChange: (v: string) => void; label?: (s: string) => string; className?: string;
}) {
  return (
    <select
      value={value ?? ''}
      onChange={(e) => onChange(e.target.value)}
      className={`min-h-[44px] rounded-xl border-0 bg-navy-800 px-3 pr-8 text-sm text-ink ring-1 ring-navy-700 focus:ring-2 focus:ring-accent ${className}`}
    >
      {value === undefined && <option value="">–</option>}
      {options.map((o) => (
        <option key={o} value={o}>
          {label(o)}
        </option>
      ))}
    </select>
  );
}

export function Slider({ value, min, max, step, unit, onCommit, format }: {
  value?: number; min: number; max: number; step: number; unit?: string;
  onCommit: (v: number) => void; format?: (v: number) => string;
}) {
  const [local, setLocal] = useState<number | undefined>(value);
  const [dragging, setDragging] = useState(false);
  useEffect(() => {
    if (!dragging) setLocal(value);
  }, [value, dragging]);
  const v = local ?? min;
  const pct = max > min ? ((v - min) / (max - min)) * 100 : 0;
  const commit = () => {
    setDragging(false);
    if (local !== undefined && local !== value) onCommit(local);
  };
  return (
    <div className="flex w-full items-center gap-4">
      <input
        type="range"
        min={min}
        max={max}
        step={step}
        value={v}
        disabled={value === undefined}
        onChange={(e) => {
          setDragging(true);
          setLocal(Number(e.target.value));
        }}
        onPointerUp={commit}
        onKeyUp={commit}
        onBlur={() => dragging && commit()}
        className="ecam-range h-2 w-full cursor-pointer appearance-none rounded-full disabled:opacity-40"
        style={{ background: `linear-gradient(90deg, #5d9cff ${pct}%, #1b2f66 ${pct}%)` }}
      />
      <span className="w-20 shrink-0 text-right font-mono text-sm tabular-nums text-ink">
        {local === undefined ? '–' : format ? format(v) : v}
        {unit && local !== undefined ? <span className="ml-1 text-ink-dim">{unit}</span> : null}
      </span>
    </div>
  );
}

// ---- controls bound to entities

export function SwitchRow({ name, hint }: { name: string; hint?: string }) {
  const e = useEntity('switch', name);
  if (!e) return null;
  return (
    <Row label={t(name)} hint={hint}>
      <Toggle checked={on(e)} onChange={(v) => call('switch', name, v ? 'turn_on' : 'turn_off')} />
    </Row>
  );
}

export function SelectRow({ name, segmented, label }: { name: string; segmented?: boolean; label?: (s: string) => string }) {
  const e = useEntity('select', name);
  if (!e || !e.option) return null;
  const set = (o: string) => call('select', name, 'set', { option: o });
  return segmented ? (
    <div className="py-2">
      <div className="mb-2 text-[15px] text-ink">{t(name)}</div>
      <Segmented options={e.option} value={str(e)} onChange={set} label={label} />
    </div>
  ) : (
    <Row label={t(name)}>
      <Dropdown options={e.option} value={str(e)} onChange={set} label={label} />
    </Row>
  );
}

export function NumberRow({ name, min, max, format, hideUnknown }: {
  name: string; min?: number; max?: number; format?: (v: number) => string; hideUnknown?: boolean;
}) {
  const e = useEntity('number', name);
  if (!e) return null;
  const v = num(e);
  if (hideUnknown && v === undefined) return null;
  return (
    <div className="py-2">
      <div className="mb-1 text-[15px] text-ink">{t(name)}</div>
      <Slider
        value={v}
        min={min ?? Number(e.min_value ?? 0)}
        max={max ?? Number(e.max_value ?? 100)}
        step={Number(e.step ?? 1)}
        unit={e.uom}
        format={format}
        onCommit={(x) => call('number', name, 'set', { value: x })}
      />
    </div>
  );
}

export function TextRow({ name }: { name: string }) {
  const e = useEntity('text', name);
  const remote = str(e) ?? '';
  const [v, setV] = useState(remote);
  const [focus, setFocus] = useState(false);
  useEffect(() => {
    if (!focus) setV(remote);
  }, [remote, focus]);
  if (!e) return null;
  const commit = () => {
    setFocus(false);
    if (v !== remote) call('text', name, 'set', { value: v });
  };
  return (
    <Row label={t(name)}>
      <input
        value={v}
        maxLength={e.max_length || undefined}
        onFocus={() => setFocus(true)}
        onChange={(ev) => setV(ev.target.value)}
        onBlur={commit}
        onKeyDown={(ev) => ev.key === 'Enter' && (ev.target as HTMLInputElement).blur()}
        className="min-h-[44px] w-44 rounded-xl border-0 bg-navy-800 px-3 text-sm text-ink ring-1 ring-navy-700 focus:ring-2 focus:ring-accent"
      />
    </Row>
  );
}

export function Press({ name, label, ...rest }: {
  name: string; label?: string; variant?: Variant; icon?: string; confirm?: boolean; className?: string; disabled?: boolean;
}) {
  const e = useEntity('button', name);
  if (!e) return null;
  const act = () => call('button', name, 'press');
  return rest.confirm ? (
    <ConfirmButton {...rest} onConfirm={act}>
      {label ?? t(name)}
    </ConfirmButton>
  ) : (
    <Button {...rest} onClick={act}>
      {label ?? t(name)}
    </Button>
  );
}

export function Value({ e, fallback = '–' }: { e?: Entity; fallback?: string }) {
  const s = e?.state && e.state !== 'NA' ? e.state : str(e);
  return <>{s ?? fallback}</>;
}

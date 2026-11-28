import { useEffect, useMemo, useState } from 'react';
import { call, num, on, str, useEntity, useStore } from './esp';
import { drink, t } from './i18n';
import {
  Button, Card, Dropdown, Icon, NumberRow, Pill, Press, Row, Segmented, SelectRow, SwitchRow, TextRow, Toggle, Value,
} from './ui';

const TABS = [
  { id: 'brew', label: 'Brew', icon: 'cup' },
  { id: 'recipes', label: 'Recipes', icon: 'recipe' },
  { id: 'beans', label: 'Beans', icon: 'bean' },
  { id: 'settings', label: 'Settings', icon: 'settings' },
  { id: 'stats', label: 'Statistics', icon: 'chart' },
  { id: 'maintenance', label: 'Maintenance', icon: 'wrench' },
  { id: 'device', label: 'Device', icon: 'chip' },
] as const;
type Tab = (typeof TABS)[number]['id'];

const PROBLEMS = [
  'Water tank', 'Water tank removed', 'Grounds container removed', 'Grounds container full', 'Drip tray',
  'Beans empty', 'Descale needed',
];

function useTab(): [Tab, (t: Tab) => void] {
  const read = () => (TABS.find((x) => `#${x.id}` === location.hash)?.id ?? 'brew') as Tab;
  const [tab, setTab] = useState<Tab>(read);
  useEffect(() => {
    const h = () => setTab(read());
    addEventListener('hashchange', h);
    return () => removeEventListener('hashchange', h);
  }, []);
  return [tab, (x) => (location.hash = x)];
}

export default function App() {
  const [tab, setTab] = useTab();
  const { connected, title } = useStore();
  const state = str(useEntity('text_sensor', 'State'));
  const online = on(useEntity('binary_sensor', 'Machine online'));

  return (
    <div className="min-h-screen bg-navy-950 bg-[radial-gradient(ellipse_at_top,_#13265a_0%,_#050b1d_60%)] font-sans text-ink antialiased">
      <div className="mx-auto flex max-w-6xl flex-col gap-6 px-4 pb-16 pt-6 md:flex-row md:px-6">
        <aside className="md:sticky md:top-6 md:h-fit md:w-56 md:shrink-0">
          <div className="mb-5 flex items-center gap-3">
            <div className="grid h-11 w-11 place-items-center rounded-2xl bg-gradient-to-br from-accent to-accent-soft text-navy-950 shadow-lg shadow-accent/30">
              <Icon name="cup" className="h-6 w-6" />
            </div>
            <div className="min-w-0">
              <div className="truncate text-lg font-semibold">{title || 'ECAM'}</div>
              <div className="flex items-center gap-1.5 text-xs text-ink-muted">
                <span className={`h-2 w-2 rounded-full ${!connected ? 'bg-rose-400' : online ? 'bg-emerald-400' : 'bg-crema'}`} />
                {!connected ? t('Connecting') : online ? t(state ?? '') : t('Machine offline')}
              </div>
            </div>
          </div>
          <nav className="-mx-4 flex gap-1 overflow-x-auto px-4 pb-1 md:mx-0 md:flex-col md:px-0">
            {TABS.map((x) => (
              <button
                key={x.id}
                type="button"
                onClick={() => setTab(x.id)}
                className={`flex shrink-0 items-center gap-3 rounded-xl px-3.5 py-2.5 text-sm font-medium transition ${
                  tab === x.id ? 'bg-navy-800 text-ink ring-1 ring-navy-600' : 'text-ink-muted hover:bg-navy-900 hover:text-ink'
                }`}
              >
                <Icon name={x.icon} className={`h-[18px] w-[18px] ${tab === x.id ? 'text-accent' : ''}`} />
                {t(x.label)}
              </button>
            ))}
          </nav>
        </aside>
        <main className="min-w-0 flex-1">
          {tab === 'brew' && <BrewTab />}
          {tab === 'recipes' && <RecipesTab />}
          {tab === 'beans' && <BeansTab />}
          {tab === 'settings' && <SettingsTab />}
          {tab === 'stats' && <StatsTab />}
          {tab === 'maintenance' && <MaintenanceTab />}
          {tab === 'device' && <DeviceTab />}
        </main>
      </div>
    </div>
  );
}

function Grid({ children }: { children: React.ReactNode }) {
  return <div className="grid gap-5 lg:grid-cols-2">{children}</div>;
}

// ---- brew

function BrewTab() {
  const { entities } = useStore();
  const state = str(entities['text_sensor/State']);
  const brewing = on(entities['binary_sensor/Brewing']);
  const progress = num(entities['sensor/Progress']);
  const alarms = (str(entities['text_sensor/Alarms']) ?? '').split(', ').filter((a) => a && a !== 'none');
  const phase = str(entities['text_sensor/Phase']) ?? '';
  const [custom, setCustom] = useState(false);
  const problems = PROBLEMS.filter((p) => on(entities[`binary_sensor/${p}`]));
  const favourites = (str(entities['text_sensor/Favourites']) ?? '').split(', ').filter((f) => f && f !== 'none');
  const off = state === 'standby' || state === 'offline';

  return (
    <div className="flex flex-col gap-5">
      <Card className="overflow-hidden">
        <div className="flex flex-wrap items-center gap-4">
          <div>
            <div className="text-xs uppercase tracking-wider text-ink-dim">{t('Status')}</div>
            <div className="mt-1 text-3xl font-semibold capitalize">{t(state ?? '–')}</div>
          </div>
          <GroundsGauge compact />
          <div className="flex w-full flex-wrap gap-2 sm:ml-auto sm:w-auto">
            {off ? (
              <Press name="Power on" variant="primary" icon="power" />
            ) : (
              <Press name="Standby" icon="power" confirm />
            )}
            <Press name="Stop" variant="danger" icon="stop" />
          </div>
        </div>
        {phase === 'aborted' && !brewing && (
          <div className="mt-4">
            <Pill tone="red">
              <Icon name="alert" className="h-3.5 w-3.5" /> {t('Brew aborted')}
            </Pill>
          </div>
        )}
        {brewing && (
          <div className="mt-5">
            <div className="mb-2 flex justify-between text-sm text-ink-muted">
              <span>{phase ? t(phase) : t('Progress')}</span>
              <span className="font-mono">{progress ?? 0} %</span>
            </div>
            <div className="h-2.5 overflow-hidden rounded-full bg-navy-700">
              <div className="h-full rounded-full bg-gradient-to-r from-accent to-crema transition-all" style={{ width: `${progress ?? 0}%` }} />
            </div>
          </div>
        )}
        <div className="mt-5 flex flex-wrap gap-2">
          {problems.length === 0 && alarms.length === 0 ? (
            <Pill tone="green">
              <Icon name="check" className="h-3.5 w-3.5" /> {t('No alarms')}
            </Pill>
          ) : null}
          {problems.map((p) => (
            <Pill key={p} tone={p === 'Descale needed' ? 'amber' : 'red'}>
              <Icon name="alert" className="h-3.5 w-3.5" /> {t(p)}
            </Pill>
          ))}
          {alarms.map((a) => (
                <Pill key={a} tone="amber">
                  {a}
                </Pill>
              ))}
        </div>
      </Card>

      <Grid>
        <Card title={t('Drink')} icon="cup">
          <DrinkDropdown className="w-full text-base" />
          <div className="mt-3">
            <Row label={t('Adjust this time')}>
              <Toggle checked={custom} onChange={setCustom} />
            </Row>
          </div>
          {custom ? (
            <>
              <RecipeFields compact />
              <div className="mt-4 flex flex-wrap gap-2">
                <Press name="Brew with changes" variant="crema" icon="play" className="flex-1 text-base" disabled={brewing || off} />
                <Press name="Reload recipe" icon="undo" />
              </div>
              <div className="mt-2 text-xs text-ink-dim">{t('Only for this cup; the stored recipe stays.')}</div>
            </>
          ) : (
            <Press name="Brew drink" variant="crema" icon="play" className="mt-4 w-full text-base" disabled={brewing || off} />
          )}
          <div className="mt-6 text-xs uppercase tracking-wider text-ink-dim">{t('Quick drinks')}</div>
          <div className="mt-2 flex flex-wrap gap-2">
            <Press name="Espresso" disabled={brewing || off} />
            <Press name="Filter style 330" disabled={brewing || off} />
          </div>
          {favourites.length > 0 && (
            <>
              <div className="mt-6 text-xs uppercase tracking-wider text-ink-dim">{t('Favourites')}</div>
              <div className="mt-2 flex flex-wrap gap-2">
                {favourites.map((f) => (
                  <Button key={f} onClick={() => call('select', 'Drink', 'set', { option: f })}>
                    {drink(f)}
                  </Button>
                ))}
              </div>
            </>
          )}
        </Card>
        <Card title={t('Profiles')} icon="settings">
          <ProfilePicker />
          <div className="mt-4 text-sm text-ink-muted">
            {t('Last brew')}: <Value e={entities['text_sensor/Last brew']} />
          </div>
        </Card>
      </Grid>
    </div>
  );
}

function ProfilePicker() {
  const { entities } = useStore();
  const sel = entities['select/Active profile'];
  if (!sel?.option) return null;
  const names = sel.option.map((o) => str(entities[`text/Profile ${o} name`]) || `${t('Profiles')} ${o}`);
  return (
    <Segmented
      options={sel.option}
      value={str(sel)}
      label={(o) => names[Number(o) - 1] ?? o}
      onChange={(o) => call('select', 'Active profile', 'set', { option: o })}
    />
  );
}

// ---- recipes

// Recipe entity -> tag name in the "Recipe limits" text ("coffee_ml 100-270, strength 0-5").
const LIMIT_TAG: Record<string, [string, number]> = {
  'Recipe coffee': ['coffee_ml', 1],
  'Recipe water': ['water_ml', 1],
  'Recipe milk': ['milk_0.1s', 10],
  'Recipe froth': ['froth', 1],
  'Recipe milk temperature': ['milk_temp', 1],
  'Recipe inversion': ['inversion', 1],
  'Recipe mug size': ['mug_size', 1],
  'Recipe ice cubes': ['ice_cubes', 1],
  'Recipe intensity': ['intensity', 1],
};

// compact: one column for the brew card, without froth (a mechanical knob on this machine; not sent).
function RecipeFields({ compact = false }: { compact?: boolean }) {
  const { entities } = useStore();
  const limitsText = str(entities['text_sensor/Recipe limits']) ?? '';
  const limits = useMemo(() => {
    const m: Record<string, [number, number]> = {};
    for (const part of limitsText.split(', ')) {
      const x = part.match(/^(\S+) (\d+)-(\d+)$/);
      if (x) m[x[1]] = [Number(x[2]), Number(x[3])];
    }
    return m;
  }, [limitsText]);
  const range = (name: string) => {
    const [tag, div] = LIMIT_TAG[name];
    const l = limits[tag];
    return l ? { min: l[0] / div, max: l[1] / div } : {};
  };
  const hasStrength = str(entities['select/Recipe strength']) !== undefined;
  const hasTemp = str(entities['select/Recipe water temperature']) !== undefined;
  return (
    <>
      <div className={`mt-5 grid gap-x-10 ${compact ? '' : 'md:grid-cols-2'}`}>
        {Object.keys(LIMIT_TAG).filter((n) => !(compact && n === 'Recipe froth')).map((n) => (
          <NumberRow key={n} name={n} hideUnknown {...range(n)} format={n === 'Recipe milk' ? (v) => v.toFixed(1) : undefined} />
        ))}
      </div>
      {(hasStrength || hasTemp) && (
        <div className={`mt-2 grid gap-x-10 ${compact ? '' : 'md:grid-cols-2'}`}>
          {hasStrength && <SelectRow name="Recipe strength" segmented />}
          {hasTemp && <SelectRow name="Recipe water temperature" segmented />}
        </div>
      )}
    </>
  );
}

function DrinkDropdown({ className }: { className: string }) {
  const drinkSel = useEntity('select', 'Drink');
  if (!drinkSel?.option) return null;
  return (
    <Dropdown
      className={className}
      options={drinkSel.option}
      value={str(drinkSel)}
      label={drink}
      onChange={(o) => call('select', 'Drink', 'set', { option: o })}
    />
  );
}

function RecipesTab() {
  const { entities } = useStore();
  const limitsText = str(entities['text_sensor/Recipe limits']) ?? '';
  const profile = str(entities['select/Active profile']);
  const profileName = profile ? str(entities[`text/Profile ${profile} name`]) : undefined;

  return (
    <Grid>
      <Card title={t('Recipe')} icon="recipe" className="lg:col-span-2">
        <div className="flex flex-wrap items-center gap-3">
          <DrinkDropdown className="min-w-[220px] flex-1 text-base" />
          {profileName && <Pill>{profileName}</Pill>}
        </div>
        <RecipeFields />
        <div className="mt-6 flex flex-wrap items-center gap-3">
          <Press name="Save recipe" variant="primary" icon="save" />
          <Press name="Reload recipe" icon="undo" />
          <span className="text-xs text-ink-dim">{t('Saved on the machine for the active profile.')}</span>
        </div>
        {limitsText && (
          <div className="mt-5 rounded-xl bg-navy-900/80 p-3 font-mono text-xs text-ink-dim ring-1 ring-navy-700">
            {t('Machine limits')}: {limitsText}
          </div>
        )}
      </Card>
    </Grid>
  );
}

// ---- beans

interface BeanSlot { slot: string; name: string; grind: string; temperature: string; aroma: string }

function parseBeans(text: string): BeanSlot[] {
  const out: BeanSlot[] = [];
  const re = /(\d) (.+?) \(grind ([\d.]+), ([^,]+), ([^)]+)\)/g;
  for (let m; (m = re.exec(text)); ) out.push({ slot: m[1], name: m[2], grind: m[3], temperature: m[4], aroma: m[5] });
  return out;
}

function BeansTab() {
  const { entities } = useStore();
  const slots = parseBeans(str(entities['text_sensor/Bean slots']) ?? '');
  const sel = entities['select/Bean slot'];
  const current = str(sel);
  const temp = entities['select/Bean temperature'];
  const aroma = entities['select/Bean aroma'];
  const used = slots.some((s) => s.slot === current);

  return (
    <Grid>
      <Card title={t('Bean slots')} icon="bean">
        <div className="grid grid-cols-2 gap-3 sm:grid-cols-3">
          {(sel?.option ?? ['1', '2', '3', '4', '5', '6']).map((n) => {
            const b = slots.find((s) => s.slot === n);
            const active = n === current;
            return (
              <button
                key={n}
                type="button"
                onClick={() => call('select', 'Bean slot', 'set', { option: n })}
                className={`rounded-xl p-3 text-left ring-1 transition ${
                  active ? 'bg-accent/15 ring-accent' : 'bg-navy-800/70 ring-navy-700 hover:ring-navy-600'
                }`}
              >
                <div className="text-xs text-ink-dim">#{n}</div>
                <div className={`mt-1 truncate font-medium ${b ? 'text-ink' : 'text-ink-dim italic'}`}>{b ? b.name : t('empty slot')}</div>
                {b && (
                  <div className="mt-1 text-xs text-ink-muted">
                    {b.grind} · {t(b.temperature)} · {t(b.aroma)}
                  </div>
                )}
              </button>
            );
          })}
        </div>
        <p className="mt-4 text-xs text-ink-dim">
          {t('The Bean Adapt espresso uses the selected slot; the machine does not report which one is active.')}
        </p>
      </Card>
      <Card title={`${t('Bean slot')} ${current ?? ''}`} icon="settings">
        <TextRow name="Bean name" />
        <NumberRow name="Bean grind" format={(v) => v.toFixed(1)} />
        {temp?.option && (
          <div className="py-2">
            <div className="mb-2 text-[15px]">{t('Bean temperature')}</div>
            <Segmented options={temp.option} value={str(temp)} onChange={(o) => call('select', 'Bean temperature', 'set', { option: o })} />
          </div>
        )}
        {aroma?.option && (
          <div className="py-2">
            <div className="mb-2 text-[15px]">{t('Bean aroma')}</div>
            <Segmented options={aroma.option} value={str(aroma)} onChange={(o) => call('select', 'Bean aroma', 'set', { option: o })} />
          </div>
        )}
        <div className="mt-5 flex flex-wrap gap-3">
          <Press name="Save bean" variant="primary" icon="save" />
          {used && <Press name="Delete bean" variant="danger" icon="trash" confirm />}
        </div>
      </Card>
    </Grid>
  );
}

// ---- settings

function SettingsTab() {
  const autostart = on(useEntity('switch', 'Autostart'));
  return (
    <Grid>
      <Card title={t('Machine')} icon="settings">
        <SelectRow name="Coffee temperature" />
        <SelectRow name="Auto-off" />
        <SelectRow name="Water hardness" />
        <SwitchRow name="Energy saving" />
        <SwitchRow name="Key beep" />
        <SwitchRow name="Cup light" />
        <SwitchRow name="12-hour clock" />
      </Card>
      <Card title={t('Autostart')} icon="power">
        <SwitchRow name="Autostart" />
        {autostart && (
          <>
            <NumberRow name="Autostart hour" format={(v) => String(v).padStart(2, '0')} />
            <NumberRow name="Autostart minute" format={(v) => String(v).padStart(2, '0')} />
          </>
        )}
      </Card>
      <Card title={t('Profiles')} icon="cup">
        <div className="mb-3">
          <ProfilePicker />
        </div>
        {[1, 2, 3, 4].map((p) => (
          <TextRow key={p} name={`Profile ${p} name`} />
        ))}
      </Card>
    </Grid>
  );
}

// ---- statistics

function StatsTab() {
  const { entities } = useStore();
  const counters = Object.values(entities)
    .filter((e) => e.id.startsWith('sensor/') && / count$/.test(e.id) && (num(e) ?? 0) > 0)
    .sort((a, b) => (num(b) ?? 0) - (num(a) ?? 0));
  const top = num(counters[0]) ?? 1;
  const cats = ['Coffees', 'Milk coffees', 'Water drinks', 'To-go drinks'];
  return (
    <div className="flex flex-col gap-5">
      <div className="grid grid-cols-2 gap-4 lg:grid-cols-4">
        {cats.map((c) => {
          const e = entities[`sensor/${c}`];
          return e ? (
            <Card key={c}>
              <div className="text-xs uppercase tracking-wider text-ink-dim">{t(c)}</div>
              <div className="mt-2 text-3xl font-semibold tabular-nums">{num(e)?.toLocaleString() ?? '–'}</div>
            </Card>
          ) : null;
        })}
      </div>
      <Card title={t('Per drink')} icon="chart">
        <div className="flex flex-col gap-3">
          {counters.map((e) => {
            const v = num(e) ?? 0;
            return (
              <div key={e.id}>
                <div className="mb-1 flex justify-between text-sm">
                  <span>{drink(e.id.slice('sensor/'.length))}</span>
                  <span className="font-mono tabular-nums text-ink-muted">{v.toLocaleString()}</span>
                </div>
                <div className="h-2 rounded-full bg-navy-800">
                  <div className="h-full rounded-full bg-gradient-to-r from-accent-soft to-accent" style={{ width: `${Math.max(1, (v / top) * 100)}%` }} />
                </div>
              </div>
            );
          })}
        </div>
      </Card>
    </div>
  );
}

// ---- maintenance

function Metric({ name, domain = 'sensor' }: { name: string; domain?: string }) {
  const e = useEntity(domain, name);
  if (!e) return null;
  const b = on(e);
  const text = b !== undefined ? t(b ? 'Yes' : 'No') : t((e.state && e.state !== 'NA' ? e.state : str(e)) ?? '–');
  if (text.length > 24)
    return (
      <div className="py-2">
        <div className="text-[15px] text-ink">{t(name)}</div>
        <div className="mt-1 break-words text-sm text-ink-muted">{text}</div>
      </div>
    );
  return (
    <Row label={t(name)}>
      <span className="font-mono tabular-nums text-ink">{text}</span>
    </Row>
  );
}

// Grounds container drawn as a bin that fills with coffee grounds; the number beside it is the
// fill level the ESP derives from PAR 40 (100 % = the machine reports "full").
const BIN = 'M17 20 H83 L77 106 Q76 111 71 111 H29 Q24 111 23 106 Z';
const BIN_TOP = 20;
const BIN_DEPTH = 91;

function GroundsGauge({ compact = false }: { compact?: boolean }) {
  const fill = num(useEntity('sensor', 'Grounds container fill'));
  const full = on(useEntity('binary_sensor', 'Grounds container full'));
  if (fill === undefined) return null;
  const pct = Math.max(0, Math.min(100, Math.round(fill)));
  const level = full || pct >= 90 ? 'text-rose-300' : pct >= 70 ? 'text-crema' : 'text-ink';
  return (
    <div className={`flex items-center ${compact ? 'gap-3' : 'gap-5 pb-2'}`}>
      <svg viewBox="0 0 100 120" className={`shrink-0 ${compact ? 'h-12 w-10' : 'h-28 w-24'}`} role="img" aria-label={`${t('Grounds container fill')}: ${pct} %`}>
        <defs>
          <clipPath id="grounds-bin">
            <path d={BIN} />
          </clipPath>
          <linearGradient id="grounds-coffee" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0" stopColor="#9a6a3f" />
            <stop offset="1" stopColor="#3f2717" />
          </linearGradient>
        </defs>
        <path d={BIN} className="fill-navy-950/60" />
        <g clipPath="url(#grounds-bin)" opacity={pct > 0 ? 1 : 0}>
          <g className="transition-transform duration-700 ease-out" style={{ transform: `translateY(${(BIN_DEPTH * (100 - pct)) / 100}px)` }}>
            <rect x="0" y={BIN_TOP} width="100" height={BIN_DEPTH + 4} fill="url(#grounds-coffee)" />
            {[22, 34, 47, 58, 70, 79].map((x, i) => (
              <circle key={x} cx={x} cy={BIN_TOP + (i % 2 ? 1.5 : 0.5)} r={i % 3 ? 3 : 4} fill="#9a6a3f" />
            ))}
          </g>
        </g>
        {[25, 50, 75].map((m) => (
          <line key={m} x1="70" x2="76" y1={BIN_TOP + (BIN_DEPTH * (100 - m)) / 100} y2={BIN_TOP + (BIN_DEPTH * (100 - m)) / 100}
            className="stroke-ink-dim" strokeWidth="1.5" strokeLinecap="round" />
        ))}
        <path d={BIN} fill="none" className={full ? 'stroke-rose-400' : 'stroke-ink-muted'} strokeWidth="3" strokeLinejoin="round" />
        <rect x="11" y="13" width="78" height="7" rx="3" className={full ? 'fill-rose-400' : 'fill-ink-muted'} />
      </svg>
      <div>
        <div className={`font-mono tabular-nums ${compact ? 'text-xl' : 'text-4xl'} ${level}`}>
          {pct}
          <span className={`${compact ? 'text-sm' : 'text-xl'} text-ink-muted`}> %</span>
        </div>
        <div className={`text-ink-muted ${compact ? 'text-xs' : 'mt-1 text-sm'}`}>{t(full ? 'Empty the grounds container' : compact ? 'Grounds container' : 'Grounds container fill')}</div>
      </div>
    </div>
  );
}

function MaintenanceTab() {
  const descale = on(useEntity('binary_sensor', 'Descale needed'));
  const grounds = on(useEntity('binary_sensor', 'Grounds container full'));
  return (
    <Grid>
      <Card title={t('Maintenance')} icon="wrench" right={descale ? <Pill tone="amber">{t('Descale needed')}</Pill> : undefined}>
        <Metric name="Water since descale" />
        <Metric name="Descales" />
        <Metric name="Water total" />
        <Metric name="Milk cleans" />
      </Card>
      <Card title={t('Grounds container')} icon="trash" right={grounds ? <Pill tone="amber">{t('Grounds container full')}</Pill> : undefined}>
        <GroundsGauge />
        <Metric name="Grounds level" />
      </Card>
      <Card title={t('Water filter')} icon="drop">
        <SwitchRow name="Water filter" />
        <Metric name="Water since filter change" />
        <Metric name="Filters installed" />
      </Card>
    </Grid>
  );
}

// ---- device

function DeviceTab() {
  return (
    <Grid>
      <Card title={t('Machine')} icon="chip">
        {['Serial number', 'Production date', 'EEPROM version', 'Accessory', 'Switches', 'Custom drinks'].map((n) => (
          <Metric key={n} name={n} domain="text_sensor" />
        ))}
      </Card>
      <Card title="ESP" icon="restart">
        <Metric name="Machine online" domain="binary_sensor" />
        <div className="mt-4">
          <Press name="Restart" icon="restart" confirm />
        </div>
      </Card>
    </Grid>
  );
}

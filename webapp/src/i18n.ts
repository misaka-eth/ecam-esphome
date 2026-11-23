// German labels (the browser language decides); everything else shows the entity's own English name.
const de: Record<string, string> = {
  // navigation
  Brew: 'Brühen', Recipes: 'Rezepte', Beans: 'Bohnen', Settings: 'Einstellungen', Statistics: 'Statistik',
  Maintenance: 'Wartung', Device: 'Gerät',
  // machine
  Offline: 'Offline', Connecting: 'Verbinde …', 'Machine offline': 'Maschine offline',
  'Power on': 'Einschalten', Standby: 'Standby', Stop: 'Stopp', 'Skip rinse': 'Spülen überspringen',
  'Brew drink': 'Zubereiten', Drink: 'Getränk', 'Active profile': 'Aktives Profil', Favourites: 'Favoriten',
  'Quick drinks': 'Schnellwahl', Espresso: 'Espresso', 'Filter style 330': 'Filterkaffee 330 ml',
  Status: 'Status', Alarms: 'Meldungen', 'No alarms': 'Keine Meldungen', Progress: 'Fortschritt',
  'Water tank': 'Wassertank leer', 'Water tank removed': 'Wassertank entnommen',
  'Grounds container removed': 'Satzbehälter entnommen', 'Grounds container full': 'Satzbehälter voll',
  'Descale needed': 'Entkalken nötig', 'Beans empty': 'Bohnen leer', 'Drip tray': 'Abtropfschale prüfen',
  Ready: 'Bereit', Brewing: 'Bereitet zu', 'Last brew': 'Letzter Bezug',
  'Adjust this time': 'Diesmal anpassen', 'Brew with changes': 'Angepasst zubereiten',
  'Only for this cup; the stored recipe stays.': 'Nur für diese Tasse; das gespeicherte Rezept bleibt.',
  'Brew aborted': 'Bezug abgebrochen',
  // phases
  grinding: 'Mahlen', 'brew unit': 'Brühgruppe', 'pre-infusion': 'Vorbrühen', extraction: 'Extraktion',
  finishing: 'Abschluss', 'milk heating': 'Milch vorbereiten', preparing: 'Vorbereiten', aborted: 'Abgebrochen',
  'waiting for beans': 'Wartet auf Bohnen',
  // states
  ready: 'bereit', standby: 'Standby', brewing: 'bereitet zu', heating: 'heizt auf', rinsing: 'spült',
  'turning on': 'schaltet ein', 'turning off': 'schaltet aus', 'milk clean': 'Milchreinigung',
  descaling: 'entkalkt', offline: 'offline', steam: 'Dampf', recovery: 'erholt sich', milk: 'Milch',
  'hot water': 'Heißwasser', chocolate: 'Schokolade', Coffees: 'Kaffees ohne Milch',
  // recipes
  Recipe: 'Rezept', 'Recipe for': 'Rezept für', 'Save recipe': 'Rezept speichern', 'Reload recipe': 'Verwerfen',
  'Recipe coffee': 'Kaffee', 'Recipe water': 'Wasser', 'Recipe milk': 'Milch', 'Recipe strength': 'Stärke',
  'Recipe water temperature': 'Wassertemperatur', 'Recipe froth': 'Milchschaum',
  'Recipe milk temperature': 'Milchtemperatur', 'Recipe inversion': 'Umkehrung', 'Recipe mug size': 'Tassengröße',
  'Recipe ice cubes': 'Eiswürfel', 'Recipe intensity': 'Intensität', 'Machine limits': 'Grenzen der Maschine',
  'Saved on the machine for the active profile.': 'Wird für das aktive Profil in der Maschine gespeichert.',
  'Not part of this drink': 'Gehört nicht zu diesem Getränk',
  // recipe options
  'pre-ground': 'Pulver', '1 extra light': '1 extra mild', '2 light': '2 mild', '3 medium': '3 mittel',
  '4 strong': '4 stark', '5 extra strong': '5 extra stark', low: 'niedrig', medium: 'mittel', high: 'hoch',
  max: 'maximal',
  // beans
  'Bean slots': 'Bohnensorten', 'Bean slot': 'Platz', 'Bean name': 'Name', 'Bean grind': 'Mahlgrad',
  'Bean temperature': 'Temperatur', 'Bean aroma': 'Aroma', 'Save bean': 'Speichern', 'Delete bean': 'Löschen',
  'empty slot': 'freier Platz', mid: 'mittel', 'very high': 'sehr hoch', 'extra mild': 'extra mild', mild: 'mild',
  normal: 'normal', strong: 'stark', 'extra strong': 'extra stark',
  'The Bean Adapt espresso uses the selected slot; the machine does not report which one is active.':
    'Der Bean-Adapt-Espresso nutzt den gewählten Platz; welcher aktiv ist, meldet die Maschine nicht.',
  'Delete this bean?': 'Diese Bohnensorte löschen?',
  // settings
  'Coffee temperature': 'Kaffeetemperatur', 'Auto-off': 'Abschaltzeit', 'Water hardness': 'Wasserhärte',
  Autostart: 'Autostart', 'Autostart hour': 'Stunde', 'Autostart minute': 'Minute', '12-hour clock': '12-Stunden-Uhr',
  'Key beep': 'Tastenton', 'Cup light': 'Tassenlicht', 'Energy saving': 'Energiesparen', 'Water filter': 'Wasserfilter',
  Profiles: 'Profile', 'Profile 1 name': 'Profil 1', 'Profile 2 name': 'Profil 2', 'Profile 3 name': 'Profil 3',
  'Profile 4 name': 'Profil 4', Machine: 'Maschine',
  // statistics
  'Drinks total': 'Getränke gesamt', 'Milk coffees': 'Kaffees mit Milch', 'Water drinks': 'Wassergetränke',
  'To-go drinks': 'To-go', 'Per drink': 'Pro Getränk', 'Water total': 'Wasser gesamt',
  // maintenance
  'Water since descale': 'Wasser seit Entkalkung', 'Water since filter change': 'Wasser seit Filterwechsel',
  Descales: 'Entkalkungen', 'Filters installed': 'Filter eingesetzt', 'Milk cleans': 'Milchreinigungen',
  'Grounds container': 'Satzbehälter', 'Grounds level': 'Füllstand',
  // device
  'Serial number': 'Seriennummer', 'Production date': 'Produktionsdatum', 'EEPROM version': 'EEPROM-Version',
  Accessory: 'Zubehör', Switches: 'Schalter', 'Custom drinks': 'Eigene Getränke', Restart: 'ESP neu starten',
  'Recipe limits': 'Rezeptgrenzen', 'Machine online': 'Maschine verbunden', Yes: 'Ja', No: 'Nein',
  'Really?': 'Sicher?', Saved: 'Gespeichert', none: 'keine',
};

const drinksDe: Record<string, string> = {
  espresso: 'Espresso', coffee: 'Kaffee', 'long coffee': 'Long Coffee', long: 'Long Coffee', '2x espresso': '2× Espresso',
  'doppio+': 'Doppio+', americano: 'Americano', cappuccino: 'Cappuccino', 'latte macchiato': 'Latte Macchiato',
  caffelatte: 'Caffelatte', 'flat white': 'Flat White', 'espresso macchiato': 'Espresso Macchiato',
  'hot milk': 'Heiße Milch', 'cappuccino doppio+': 'Cappuccino Doppio+', 'cappuccino reverse': 'Cappuccino Reverse',
  'hot water': 'Heißes Wasser', tea: 'Tee', 'coffee pot': 'Kaffeekanne', cortado: 'Cortado',
  'brew over ice': 'Brew over Ice', 'iced americano': 'Iced Americano', 'iced cappuccino': 'Iced Cappuccino',
  'iced latte macchiato': 'Iced Latte Macchiato', 'iced cappuccino mix': 'Iced Cappuccino Mix',
  'iced flat white': 'Iced Flat White', 'iced cold milk': 'Kalte Milch', 'iced caffelatte': 'Iced Caffelatte',
  'bean adapt espresso': 'Bean-Adapt-Espresso',
};

export const isDe = typeof navigator !== 'undefined' && navigator.language.toLowerCase().startsWith('de');

export function t(s: string): string {
  return isDe ? de[s] ?? s : s;
}

// Drink names: "mug ..." keeps the base name with a mug prefix, "custom n" stays numbered.
export function drink(s: string): string {
  const name = s.replace(/ count$/i, '').toLowerCase();
  if (!isDe) return (name === 'long' ? 'long coffee' : name).replace(/(^|[\s+-])\w/g, (c) => c.toUpperCase()).replace(/\b2x\b/i, '2×');
  if (name.startsWith('mug ')) return `${drinksDe[name.slice(4)] ?? name.slice(4)} (Becher)`;
  if (name.startsWith('custom ')) return `Eigenes Getränk ${name.slice(7)}`;
  return drinksDe[name] ?? name;
}

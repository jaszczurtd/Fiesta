/* Shared presentation language. Wire keys and recorded text are never translated. */
(function () {
  'use strict';
  const VP37 = window.VP37 = window.VP37 || {};
  const polish = {
  "Missing data file ": "brak pliku danych ",
  "loop period": "okres pętli",
  "feedback age": "wiek feedbacku",
  "other": "inne",
  "Requested dose": "Zadana dawka",
  "ON current": "Prąd fazy ON",
  "Loop period": "Okres pętli",
  "Feedback age": "Wiek pomiaru",
  "Supply voltage": "Napięcie zasilania",
  "Fuel temperature": "Temperatura paliwa",
  "No metrics in recognized sections of this file.": "Plik nie ma metryk w znanych sekcjach.",
  "No samples in this window.": "Brak próbek w oknie.",
  "samples": "próbki",
  "samples: ": "próbek: ",
  "Samples in window: ": "Próbek w oknie: ",
  ". Axis ": ". Oś ",
  "; outside axis: ": "; poza osią: ",
  " (below ": " (poniżej ",
  ", above ": ", powyżej ",
  "). Minimum ": "). Minimum ",
  ", maximum ": ", maksimum ",
  " Rows without a value: ": " Wierszy bez wartości: ",
  "From metrics file (": "Z pliku metryk (",
  "): median ": "): mediana ",
  ". These values were calculated by the gate.": ". Te liczby policzyła bramka; strona ich nie liczy.",
  "Iteration/series: ": "Iteracja/seria: ",
  " · raw position log for ": " · log pozycji surowej przebiegu ",
  " · raw position: ": " · pozycja surowa: ",
  "role unspecified": "rola nieopisana",
  "repeat ": "powtórzenie ",
  "variant ": "wariant ",
  " description: ": " opis: ",
  "role unspecified (iteration has no runs.json)": "rola nieopisana (brak runs.json iteracji)",
  "Log time moves backwards ": "Czas w logu cofa się ",
  " time(s); subsequent lines were offset to keep time increasing.": " raz(y); dalsze linie przesunięto, by czas rósł.",
  "Fields missing from the parser catalog: ": "Pola nieopisane w katalogu parsera: ",
  "Unknown VP37 line types: ": "Nieznane rodzaje linii VP37: ",
  "Revision (first CFG line)": "Rewizja (pierwsza linia CFG)",
  " (changes: ": " (zmiany: ",
  "no CFG line": "brak linii CFG",
  "Log SHA-256 (decompressed text)": "SHA-256 logu (tekst po rozpakowaniu)",
  "Copies of this log": "Kopie tego samego logu",
  "missing": "brak",
  "Recording duration": "Długość nagrania",
  "VP37 C / IPULSE lines": "Linie VP37 C / IPULSE",
  "no image.json": "brak image.json",
  "ECU image (SHA-256)": "Obraz ECU (SHA-256)",
  "Adjustometer image (SHA-256)": "Obraz Adjustometera (SHA-256)",
  "Fiesta / HAL commit": "Commit Fiesty / HAL",
  "Definitions and mode": "Definicje i tryb",
  "Iteration base-commit.txt": "base-commit.txt iteracji",
  "Metrics from recorded files": "Metryki z pliku metryk",
  "No metrics files assigned. Metrics are calculated by the gate script (analyze_gate.py).": "Brak przypisanych plików metryk. Przeglądarka nie liczy metryk; liczy je skrypt bramki (analyze_gate.py).",
  "Analysis window: ": "Okno analizy: ",
  "The file did not record an analysis window.": "Plik nie zapisał okna analizy.",
  "Analyzer warnings: ": "Ostrzeżenia analizatora: ",
  "Nearby metrics files belonging to other runs": "Pliki metryk obok, które nie należą do tego przebiegu",
  "belongs to ": "należy do ",
  "Test timeline": "Oś testów",
  "Click a test to zoom in": "kliknij test, aby powiększyć",
  " (no end recorded)": " (bez końca w logu)",
  "window: ": "okno: ",
  "Traces": "Przebiegi",
  "Full log": "Cały log",
  "Analysis window": "Okno analizy",
  "Zoom out ×2": "oddal ×2",
  "Additional series:": "Dodatkowe serie:",
  "Drag on a chart to zoom in; double-click to reset. Views shorter than ": "Przeciągnij po wykresie, aby powiększyć; dwuklik wraca do całości. Widok węższy niż ",
  " s show all samples; wider views show each interval's minimum and maximum.": " s pokazuje wszystkie próbki, szerszy minimum i maksimum każdego przedziału.",
  "Loading full samples...": "wczytywanie pełnych próbek...",
  "Full samples (": "pełne próbki (",
  " fragments)": " fragm.)",
  "Downsampled: min/max over ": "przerzedzenie: min/max w ",
  "Histograms in the analysis window": "Histogramy w oknie analizy",
  "Window: ": "Okno: ",
  " s). Counts include all samples; the axis may cover a smaller range.": " s). Wszystkie próbki wchodzą do liczb; oś może być węższa.",
  "Telemetry fields in this log (": "Pola telemetrii w tym logu (",
  "undocumented": "nieopisane",
  "First CFG line": "Pierwsza linia CFG",
  "Iteration report.md": "report.md iteracji",
  "Section: ": "Sekcja: ",
  "Full report.md": "Cały report.md",
  " versus ": " wobec ",
  "before": "przed",
  "after": "po",
  "raw position": "pozycja surowa",
  "filtered": "po filtrze",
  "source: ": "źródło: ",
  "Metrics worse than references (threshold 0): ": "Metryk gorszych od referencji (próg 0): ",
  " of ": " z ",
  "; worst change ": "; najgorsza zmiana ",
  "A/B comparison: ": "Porównanie A/B: ",
  "All comparison metrics": "Wszystkie metryki porównania",
  "metric": "metryka",
  "candidate": "kandydat",
  "change": "zmiana",
  "gate assessment": "ocena bramki",
  "worse": "gorzej",
  "worse metrics": "gorszych",
  "no increase": "bez wzrostu",
  " (small change)": " (mała zmiana)",
  "report.md, section: ": "report.md, sekcja: ",
  "Run and comparison descriptions: ": "Opis przebiegów i porównań: ",
  "This iteration has no run and comparison description (runs.json). Run roles are unspecified.": "Iteracja nie ma opisu przebiegów i porównań (runs.json). Role przebiegów nie są zgadywane.",
  " Description error: ": " Błąd opisu: ",
  "Runs (": "Przebiegi (",
  "run": "przebieg",
  "kind": "rodzaj",
  "repeat": "powt.",
  "variant": "wariant",
  "duration": "długość",
  "tests": "testy",
  "metrics": "metryki",
  "Runs relative to the reference median": "Przebiegi względem mediany referencji",
  "No described references: this chart requires roles from runs.json.": "Brak opisanych referencji: wykres wymaga ról z runs.json.",
  "core": "podstawowe",
  "Metrics: ": "Metryki: ",
  "Filled marker: filtered; hollow: raw position versus raw references. Gray: reference; color: candidate variant.": "pełny znacznik: po filtrze, pusty: pozycja surowa wobec surowych referencji; szary: referencja, kolor: wariant kandydata",
  " % of reference median": " % mediany ref.",
  "No metrics in this family.": "Brak metryk tej rodziny.",
  "% of reference median": "% mediany referencji",
  "runs": "przebiegi",
  "Comparisons (": "Porównania (",
  "No described comparisons.": "Brak opisanych porównań.",
  "Metrics files in this iteration": "Pliki metryk w iteracji",
  "VP37 measurements": "Pomiary VP37",
  "gate iterations": "iteracje bramki",
  "measurement series": "serie pomiarów",
  "iterations without comparison descriptions": "iteracje bez opisu porównań",
  "current baseline": "obowiązująca baza",
  "logs without VP37 telemetry": "logi bez telemetrii VP37",
  "all series": "wszystkie serie",
  "all": "wszystkie",
  "any": "każda",
  "series...": "seria...",
  "Comparison map": "Mapa porównań",
  "Each row compares a candidate to its references. Color shows the gate assessment at threshold 0 (red: an increase against any reference). A pale shade indicates a change below ": "Wiersz to jedno porównanie kandydata z jego referencjami. Kolor: ocena bramki przy progu 0 (czerwony: metryka wzrosła wobec którejkolwiek referencji). Blady odcień: zmiana poniżej ",
  " %, for display only. Click a cell to open the A/B comparison.": " %, tylko wizualnie. Kliknięcie komórki otwiera porównanie A/B.",
  "Scope ": "Zakres ",
  "Metrics ": "Metryki ",
  "Revision ": "Rewizja ",
  "From ": "Od ",
  "To ": "Do ",
  "Reference trend": "Linia referencji",
  "One point per iteration: the median of its filtered references for the selected metric.": "Jeden punkt na iterację: mediana jej referencji (po filtrze) dla wybranej metryki.",
  "Metric ": "Metryka ",
  "comparison": "porównanie",
  "worst": "najgorsza",
  "no comparison description": "brak opisu porównań",
  "raw": "surowa",
  "\ncandidate ": "\nkandydat ",
  "\nreferences ": "\nreferencje ",
  "No described references with this metric.": "Brak opisanych referencji z tą metryką.",
  "reference median": "mediana referencji",
  "- select a run -": "- wybierz przebieg -",
  "A/B comparison": "Porównanie A/B",
  "Steps: ": "Skoki: ",
  "position as % of step": "położenie w % skoku",
  "The iteration's report.md records the gate verdict.": "Strona pokazuje liczby; werdykt bramki wydaje report.md iteracji (AGENTS.md §12).",
  "Overlaid steps, aligned to the step start": "Skoki nałożone, wyrównane do chwili skoku",
  "No matching steps (top/topzero tests) in both runs.": "Brak wspólnych skoków (testy top/topzero) w obu przebiegach.",
  "% of step": "% skoku",
  "Position [Hz]": "adj Hz",
  "Steps A: ": "Skoki A: ",
  ". Measured position relative to the TEST line timestamp (axis in ms).": ". Położenie adj względem chwili linii TEST (oś w ms).",
  "Metrics A -> B": "Metryki A -> B",
  "Select two runs.": "Wybierz dwa przebiegi.",
  "No assigned metrics (": "Brak przypisanych metryk (",
  ") for ": ") dla ",
  ". This page does not calculate them.": ". Strona ich nie liczy.",
  "B metrics worse than A (threshold 0): ": "Metryk B gorszych od A (próg 0): ",
  "assessment (threshold 0)": "ocena (próg 0)",
  "Page error: ": "Błąd strony: ",
  "Connecting...": "Łączenie...",
  "Pause": "Pauza",
  "Cursor: move over a chart": "Kursor: przesuń mysz nad wykresem",
  "complete": "kompletny",
  "INCOMPLETE": "NIEKOMPLETNY",
  "VP37 scope": "Oscyloskop VP37",
  "Full paused range": "Cały zatrzymany zakres",
  "60 s window. Pause and zoom retain the view while data continues to arrive. Drag to zoom; double-click to reset. ": "Okno 60 s. Pauza i zoom zachowują zakres przy dalszym odbiorze. Przeciągnij, aby powiększyć; dwuklik przywraca zakres. ",
  "Live axis: view seconds, with a 0.1 s gap between sessions (not the disconnection duration). T trace: seconds from the first sample. CSV also retains source time and segment number.": "Oś live: sekundy widoku, przerwa między sesjami = 0,1 s (nie czas rozłączenia). Ślad T: sekundy od pierwszej próbki. CSV zachowuje także czas źródłowy i numer segmentu.",
  "Resume live": "Wznów live",
  " | paused": " | zatrzymany",
  " | ignored unknown fields: ": " | pominięte nieznane pola: ",
  "Current stream": "Bieżący strumień",
  " - complete": " - kompletny",
  " - INCOMPLETE": " - NIEKOMPLETNY",
  " - paused": " - zatrzymany",
  "INCOMPLETE: ": "NIEKOMPLETNY: ",
  "Receiving T: ": "Odbieranie T: ",
  " samples (waiting for trace end)": " próbek (oczekiwanie na koniec zrzutu)",
  " - no data in range": " - brak pola w zakresie",
  "Field catalog: HTTP ": "Katalog pól: HTTP ",
  "WebSocket port": "Port WebSocket",
  "Connected": "Połączono",
  "Waiting for hello": "Oczekiwanie na hello",
  "Waiting for log file": "Oczekiwanie na plik logu",
  "Disconnected - retrying, no replay": "Rozłączono - ponawianie, bez odtwarzania",
  "View error: ": "Błąd widoku: ",
  "Reception started": "Początek odbioru",
  "Sample count differs from trace end": "Liczba próbek różni się od końca zrzutu",
  "Disconnected - lost data is not replayed": "Rozłączenie - brak odtwarzania utraconych danych",
  "Unsupported protocol": "Nieobsługiwany protokół",
  "New connection - no replay": "Nowe połączenie - brak odtwarzania",
  "File / epoch changed": "Zmiana pliku / epoki",
  "Invalid batch": "Błędna paczka",
  "Stream gap": "Luka w strumieniu",
  "Missing MCU timestamp us": "Brak czasu MCU us",
  "Next trace started before trace end": "Kolejny ślad rozpoczęty przed zakończeniem poprzedniego",
  "Trace time is not increasing": "Czas śladu nie rośnie",
  "Trace sample limit exceeded": "Przekroczony limit próbek śladu",
  "MCU clock restarted": "Restart zegara MCU",
  "Comparison overview": "Przegląd porównań",
  "Saved measurements": "Zapisane pomiary",
  "VP37 - live": "VP37 - na żywo"
};
  Object.assign(polish, {
  "Position": "Położenie",
  "Compare the requested target, slew-limited target and measured position relative to the sensor baseline.": "Porównaj cel zadany, cel po ograniczeniu szybkości zmian i pomiar położenia względem bazy czujnika.",
  "Position error": "Uchyb położenia",
  "Positive error means the measured position is below the control target.": "Dodatni uchyb oznacza pomiar położenia poniżej celu regulatora.",
  "Sensor frequency": "Częstotliwość czujnika",
  "Raw and filtered frequencies include the baseline; position is shown separately.": "Częstotliwość surowa i filtrowana obejmują bazę; położenie ma osobny wykres.",
  "Actuator command": "Sterowanie nastawnika",
  "Compare the final PWM output with the base command before PID correction.": "Porównaj końcowe wyjście PWM ze sterowaniem bazowym przed korektą PID.",
  "PID correction": "Korekta PID",
  "Individual PID contributions and their combined, limited correction; limits apply to the correction.": "Wkłady członów PID i ich łączna korekta po ograniczeniu; granice dotyczą korekty.",
  "Controller states": "Stany regulatora",
  "Filled bands indicate active states; faint bands indicate inactive states.": "Pełne pasy oznaczają stan aktywny, blade - nieaktywny.",
  "Current": "Prąd",
  "ON-phase current statistics, the current target and the observed current error.": "Statystyki prądu fazy ON, cel prądu i obserwowany uchyb prądu.",
  "Current correction": "Korekta prądu",
  "Current control contribution to the PWM command, in native counts.": "Wkład regulacji prądu do polecenia PWM, w jego jednostkach.",
  "Supply voltage": "Napięcie zasilania",
  "Compare the voltage used by the loop with the value retained by compensation.": "Porównaj napięcie używane przez pętlę z wartością trzymaną przez kompensację.",
  "Voltage compensation": "Kompensacja napięcia",
  "Voltage correction factor applied by the control loop.": "Współczynnik korekty napięciowej stosowany przez pętlę sterowania.",
  "Thermal compensation": "Kompensacja termiczna",
  "Thermal scaling used by the control loop.": "Skala termiczna używana przez pętlę sterowania.",
  "Fuel temperature": "Temperatura paliwa",
  "Fuel temperature used by the controller.": "Temperatura paliwa używana przez regulator.",
  "Loop timing": "Czasy pętli",
  "Compare the loop period, feedback age and I2C read duration.": "Porównaj okres pętli, wiek pomiaru i czas odczytu I2C.",
  "Execution timing": "Czasy wykonania",
  "Time spent collecting the current scan and executing the control step.": "Czas zbierania skanu prądu i wykonywania kroku sterowania.",
  "Adjustometer frequency": "Częstotliwość Adjustometera",
  "Frequency reported directly by the position sensor.": "Częstotliwość zgłaszana bezpośrednio przez czujnik położenia.",
  "Baseline deviation": "Odchyłka od bazy",
  "Signed deviation from the sensor baseline, in Hz.": "Odchyłka od bazy czujnika ze znakiem, w Hz.",
  "Adjustometer supply": "Zasilanie Adjustometera",
  "Raw reading in decivolts: 120 dV corresponds to 12 V.": "Surowy odczyt w decywoltach: 120 dV odpowiada 12 V.",
  "Adjustometer temperature": "Temperatura Adjustometera",
  "Fuel temperature reported directly by the sensor.": "Temperatura paliwa zgłaszana bezpośrednio przez czujnik.",
  "Adjustometer status": "Status Adjustometera",
  "Each band shows one status bit. All clear means no reported fault; unnamed bits retain their numbers.": "Każdy pas pokazuje jeden bit statusu. Wszystkie wyzerowane oznaczają brak zgłoszonego błędu; nieopisane bity zachowują numery.",
  "Undocumented field": "Nieopisane pole",
  "Unknown status bit": "Nieopisany bit statusu",
});
  const valid = (v) => v === 'en' || v === 'pl';
  let language = 'en';
  try {
    const query = new URLSearchParams(location.search).get('lang');
    const saved = localStorage.getItem('vp37.language');
    language = valid(query) ? query : valid(saved) ? saved : 'en';
  } catch (_) {
    const query = new URLSearchParams(location.search).get('lang');
    if (valid(query)) language = query;
  }
  const t = (text) => language === 'pl' ? (polish[text] || text) : text;
  function href(value) {
    const url = new URL(value, location.href);
    url.searchParams.set('lang', language);
    return url.href;
  }
  function header() {
    document.documentElement.lang = language;
    const titles = ['VP37 measurements', 'A/B comparison', 'VP37 - live'];
    const title = titles.find((text) => document.title === text || document.title === polish[text]);
    if (title) document.title = t(title);
    document.querySelectorAll('[data-i18n]').forEach((node) => node.textContent = t(node.dataset.i18n));
    document.querySelectorAll('header.top a').forEach((node) => node.href = href(node.href));
    document.querySelectorAll('.language-switch').forEach((button) => {
      button.textContent = language === 'en' ? 'EN -> PL' : 'PL -> EN';
      button.lang = language === 'en' ? 'pl' : 'en';
      button.title = language === 'en' ? 'Przełącz na polski' : 'Switch to English';
      button.setAttribute('aria-label', button.title);
    });
  }
  function setLanguage(value) {
    if (!valid(value) || language === value) return;
    language = value;
    try { localStorage.setItem('vp37.language', value); } catch (_) { /* Storage can be disabled in webviews. */ }
    try { history.replaceState(null, '', href(location.href)); } catch (_) { /* file:// may disallow this. */ }
    header();
    window.dispatchEvent(new Event('vp37-language'));
  }
  function mount(parent) {
    const button = document.createElement('button');
    button.className = 'language-switch'; button.type = 'button';
    button.onclick = () => setLanguage(language === 'en' ? 'pl' : 'en');
    parent.appendChild(button); header();
    return button;
  }
  function field(info, kind, key) {
    const id = kind + '.' + key;
    const label = info && (language === 'pl' ? info.label_pl : info.label);
    const description = info && (language === 'pl' ? info.description_pl : info.description);
    const unit = info ? (info.unit || '') : '';
    return { id, name: label || id, unit,
      description: id + (unit ? ' [' + unit + ']' : '') + '\n' +
        (description || t('Undocumented field')) };
  }
  // Generated diagnostic templates may contain paths or marker names. Translate
  // only the template; retain captured source values verbatim.
  const diagnosticTemplates = [
  [
    "przypisane",
    "assigned"
  ],
  [
    "przypisane, plik nie zapisał okna analizy",
    "assigned, no analysis window recorded"
  ],
  [
    "nieprzypisane: plik nie ma pola source",
    "unassigned: no source field"
  ],
  [
    "nieprzypisane: log ze źródła nie istnieje",
    "unassigned: source log missing"
  ],
  [
    "nieprzypisane: źródło nie jest logiem przebiegu VP37",
    "unassigned: source is not a VP37 run log"
  ],
  [
    "nieprzypisane: okno analizy nie pasuje do logu",
    "unassigned: analysis window does not match the log"
  ],
  [
    "nieprzypisane: log różni się od opisanego w runs.json",
    "unassigned: log differs from runs.json"
  ],
  [
    "nieczytelny plik metryk",
    "unreadable metrics file"
  ],
  [
    "okno whole_log ma inne granice niż log",
    "whole_log window boundaries differ from the log"
  ],
  [
    "brak '{0}' w chwili {1}",
    "missing '{0}' at time {1}"
  ],
  [
    "nieznany rodzaj okna '{0}'",
    "unknown window kind '{0}'"
  ],
  [
    "katalog przebiegu przeniesiony: log znaleziony jako {0}",
    "run directory moved: log found as {0}"
  ],
  [
    "cały log (brak przypisanych metryk z oknem)",
    "full log (no assigned metrics with a window)"
  ],
  [
    "okno analizy z {0}",
    "analysis window from {0}"
  ],
  [
    "brak albo nieczytelny {0}",
    "missing or unreadable {0}"
  ],
  [
    "kilku kandydatów bez pliku porównania: wartości tylko na stronie iteracji",
    "multiple candidates without a comparison file: values shown on the iteration page only"
  ],
  [
    "{0}: brak przypisanych metryk ({1})",
    "{0}: no assigned metrics ({1})"
  ],
  [
    "nazwy katalogów",
    "directory names"
  ],
  [
    "wstęp",
    "introduction"
  ]
].map(([pl, en]) => {
    const pattern = pl.split(/(\{\d+\})/).map((part) => /^\{\d+\}$/.test(part) ? '(.*?)' :
      part.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).join('');
    return { pattern: new RegExp('^' + pattern + '$'), en };
  });
  function diagnostic(text) {
    if (!text || language === 'pl') return text;
    for (const item of diagnosticTemplates) {
      const match = item.pattern.exec(text);
      if (match) return item.en.replace(/\{(\d+)\}/g, (_, n) => match[Number(n) + 1]);
    }
    return text;
  }
  VP37.i18n = { diagnostic, t, href, setLanguage, mount, field, get language() { return language; } };
  document.documentElement.lang = language;
})();

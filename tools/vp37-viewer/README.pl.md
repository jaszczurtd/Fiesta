# Przeglądarka pomiarów VP37

[English](README.md)

Narzędzie tworzy strony HTML z logów collectora VP37, zapisanych metryk
i opisów przebiegów. Pokazuje pojedyncze nagrania, iteracje i porównania.
Strony działają przez `file://`, bez serwera i zewnętrznych bibliotek wykresów.

## Build i uruchomienie

Z katalogu Fiesty, po przygotowaniu przypiętych zależności HAL:

```sh
cmake -S tools/vp37-viewer -B tools/vp37-viewer/.build -DCMAKE_BUILD_TYPE=Release
cmake --build tools/vp37-viewer/.build --parallel
ctest --test-dir tools/vp37-viewer/.build --output-on-failure
```

Program powstaje jako `tools/vp37-viewer/.build/vp37-viewer`; generator
z wieloma konfiguracjami umieszcza go w katalogu wybranej konfiguracji.

Dla generatora Visual Studio podaj przy konfiguracji `-A x64` lub `-A Win32`,
przy budowaniu `--config Release`, a przy uruchamianiu CTest `-C Release`.

```sh
vp37-viewer parse /path/to/recording.log.gz
vp37-viewer build --root /path/to/recordings --site /path/to/pages \
  --cache /path/to/cache --descriptions /path/to/descriptions
vp37-viewer describe --root /path/to/recordings --cache /path/to/cache \
  --descriptions /path/to/descriptions
```

Wszystkie katalogi danych i wyników trzeba podać jawnie. Pod katalogiem
nagrań znajdują się `vp37_implementation_tests/`, `measurements/` lub
`archive/measurements/`. Nagrania są tylko do odczytu. Na strony i cache
przeznacz osobne katalogi: `build` zastępuje wygenerowane pliki i usuwa
z nich nieaktualne wyniki. `describe` wypisuje proponowane opisy;
`--write` je zapisuje, a `--overwrite` pozwala zastąpić istniejące opisy.

Czytnik obsługuje `.log` i pliki `.log.gz` z jednym członem gzip. Archiwa
pomiarów `.tar.gz` trzeba wcześniej rozpakować. Metryki są wiązane po
tożsamości logu i oknie analizy. Po przeniesieniu danych czytnik rozpoznaje
katalog obszaru pomiarów, bez zależności od nazwy workspace.

## Serwer rosnącego logu

```sh
vp37-viewer serve --log /path/to/collector.log --site /path/to/pages
```

Serwer czyta log od początku, śledzi dopisywane bajty i udostępnia wcześniej
wygenerowane strony offline przez HTTP. Oba listenery działają wyłącznie na
`127.0.0.1`: HTTP na porcie 8037, WebSocket na 8038. Porty zmieniają opcje
`--port` i `--ws-port`. Ctrl+C zatrzymuje program. Log pozostaje tylko do odczytu;
serwer nie otwiera portu szeregowego ani nie tworzy brakującego logu.

Opcjonalne `--site` udostępnia strony pod `/`; bez niego `/` zwraca stan serwera.
`/api/status` zawsze zwraca stan w JSON, a `/assets/` pliki interfejsu dostarczone
z programem. Pliki statyczne muszą być mniejsze niż 512 KiB; pliki osiągające
ten limit lub większe otrzymują HTTP 413. Ścieżki poza wskazanymi katalogami są odrzucane. HTTP i WebSocket
obsługują po czterech klientów.

Endpoint WebSocket to `ws://127.0.0.1:8038/ws`. Wersja 1 protokołu wysyła:

| Wiadomość | Znaczenie |
| --- | --- |
| `hello` | `protocol`, bieżąca `epoch`, numer następnej paczki `next`, obecność logu `present` i `ws_port`. |
| `reset` | Te same pola po pojawieniu się, zniknięciu, obcięciu lub podmianie logu. Klient odrzuca bufory poprzedniej epoki. |
| `samples` | `epoch`, rosnący `seq` i `records`. Rekord zawiera `kind`, czas `t` w sekundach oraz liczbowe `values` albo tekst zdarzenia `text`. Brakujące i nieskończone wartości to JSON `null`. |

Paczki wychodzą zwykle co 40 ms, a po osiągnięciu 32 KiB wcześniej. Rekordy
korzystają z czasu MCU, gdy jest dostępny, według zasad poniżej. `HOST`, `TEST`
i pozostałe komunikaty VP37, w tym koniec śladu, zachowują tekst. Nie oznacza
to dodania interpretacji rekordów kalibracji.

Nowe połączenie otrzymuje kolejne paczki, bez odtwarzania wcześniejszej historii.
Numery paczek pozwalają wykryć luki. Po ponownym połączeniu klient zaczyna od
`hello`; restart serwera rozpoczyna nowy strumień. Klient, który nie przyjmuje
całej ramki, zostaje rozłączony. Serwer nie kolejkuje danych ani nie ponawia
częściowo wysłanej ramki. Odrzuca wiadomości danych od klientów; endpoint służy
wyłącznie do publikowania pomiarów.

Czytnik zachowuje niepełną linię na końcu pliku i odrzuca ją po wykryciu zmiany
logu. Rozpoznaje ją po tożsamości pliku, rozmiarze i ostatnich 64 odczytanych
bajtach. Nadpisania, które między odczytami odtworzy te same bajty i rozmiar,
nie da się odróżnić od zwykłego dopisywania. Przy rotacji używaj atomowej podmiany.
Jedno wywołanie `poll` czyta najwyżej 64 KiB. Limit linii wynosi 64 KiB,
rekordu liczbowego 256 różnych pól, a rekordu po zakodowaniu 32512 bajtów.
Przekroczenie limitu lub błąd odczytu kończy program z błędem. Na brakujący plik
serwer czeka. Historia parsera i kolejki klientów nie rosną z długością logu.

Wykresy na żywo i integracja VS Code stanowią osobne etapy. Istniejące strony
offline nadal działają przez `file://`.

## Parser

`src/telemetry.h` udostępnia `TelemetryParser::feed()` i `finish()`. Fragment
wejścia może skończyć się w dowolnym miejscu linii; przed `finish()` parser
oddaje tylko kompletne linie. Limit linii to 64 KiB. Błąd zatrzymuje dany
czytnik; po obcięciu lub podmianie pliku odrzuć częściowy wynik i utwórz nowy.

Domyślna oś zachowuje czas collectora, zgodnie z oknami zapisanych metryk.
Opcjonalny tryb MCU obsługuje przepełnienie `us` i umieszcza opóźnione próbki
`T`, `IPULSE` i `IWAVE` względem rekordów sterowania. Cofnięcie czasu
sterowania rozpoczyna nową epokę; kolejne obserwacje sterowania muszą dzielić
odstępy mniejsze niż 2^31 mikrosekund. Bez jawnego znacznika nie da się odróżnić
restartu blisko przepełnienia od przepełnienia. Linie bez `us` zachowują czas collectora.
Pola numeryczne każdego `CFG` są przechowywane osobno od podsumowań rewizji.

`parse_log()` korzysta z tego samego czytnika. Dzielenie na linie i obliczenia
czasu są w C11; istniejący model nagrania i generator stron pozostają w C++17.
Zmiana wersji parsera unieważnia starszy cache po zmianie jego wyników.

Konstruktor z callbackiem przekazuje pojedyncze rekordy bez zachowywania
nagrania. Przekazane wartości są ważne tylko podczas callbacka; zwrócony błąd
zatrzymuje czytnik. Parsowanie offline zachowuje dotychczasowe działanie.
Zapis z monitora, wykresy na żywo, integracja VS Code i interpretacja rekordów
kalibracji (`CAL`) czekają na implementację. Viewer offline zgłasza nieznane pola
i rodzaje linii.

Testy przeglądarki uruchamia również hostowy build ECU, w lokalnej bramce
i CI. Czytają wyłącznie niezależne dane z `tests/fixtures/`.

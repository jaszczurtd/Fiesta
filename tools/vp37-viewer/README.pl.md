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

## Parser i przygotowanie trybu na żywo

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

Serwer na żywo, zapis z monitora, integracja VS Code i rekordy kalibracji
(`CAL`) czekają na implementację. Viewer zgłasza nieznane pola i rodzaje linii.

Testy przeglądarki uruchamia również hostowy build ECU, w lokalnej bramce
i CI. Czytają wyłącznie niezależne dane z `tests/fixtures/`.

# Testy hostowe i CI

[English](TESTING.md) · [Przygotowanie środowiska](BUILDING.pl.md) · [MISRA](../MISRA.pl.md)

Rejestr [`modules.json`](../modules.json) wyznacza listy lokalnych buildów
firmware i testów hostowych. Moduły firmware z polem `hostTests` używają mocka
HAL i testów kompilowanych jako C++; SerialConfigurator ma własny projekt
w C. Fiesta_clock ma buildy firmware oraz test wyświetlacza uruchamiany
przez główny zestaw testów Pythona. Kompiluje on kod ekranów, renderer HAL
i sterownik SSD1306 z symulowaną obsługą wejść/wyjść. Model pamięci
kontrolera interpretuje adresowanie i zapis danych I²C, a obrazy znaków
w obu rozmiarach są porównywane z zapisami ze starego sterownika. Testy
obejmują też komendy usypiania i budzenia. Drugi test kompiluje kod obsługi
czasu i CAN zegara ze sterownikiem RTC z HAL, modelem rejestrów PCF8563,
modelem DS18B20 i symulowanymi wywołaniami CAN, flasha oraz konfiguratora.
Sprawdza znacznik UTC, przeliczanie CET/CEST, ustawianie RTC według UTC
z GPS, ramki `0x130` i `0x132` oraz zmianę czasu przyciskami
i konfiguratorem. Testy wymagają kompilatora C/C++
na hoście, bez podłączonej płytki. Nie potwierdzają zgodności fizycznego
panelu ani nie mierzą czasu działania na sprzęcie. VP37TestBench uczestniczy w lokalnej kompilacji i testach,
ale `ci: false` wyłącza go z macierzy firmware CI.

## Kontrole lokalne

Po [przygotowaniu narzędzi](BUILDING.pl.md#przygotowanie-linuksa-lub-wsl)
uruchom z katalogu głównego:

```bash
./runalltests.sh -j8
```

Runner sprawdza dostępność narzędzi, buduje projekty hostowe, uruchamia CTest
z `-LE static-analysis`, a następnie cppcheck dla modułów, które go wybrały,
bramkę MISRA, PMD CPD, Valgrind i clang-tidy. Analiza statyczna działa osobno,
żeby jej czas nie wyglądał jak zawieszony test wykonania. Logi trafiają do
`/tmp/fiesta_*`.

Każdy moduł z testami hostowymi jest budowany i testowany dwa razy: w Debug
oraz w Release, tak jak w CI, w `<katalog buildu>/release` (np.
`src/SerialConfigurator/build/release`). Release sprawdza też optymalizację
i włączone przez kompilator mechanizmy ochrony libc. Etapy Valgrinda
i clang-tidy sprawdzają oba buildy każdego modułu.

Testy GUI korzystają z bieżącego ekranu albo Xvfb przez
`scripts/sc-with-display.sh`. Brak ekranu i Xvfb jest błędem. Widok pomiarów
Clocks jest sprawdzany w bieżącym locale oraz jawnie w `C` i każdym locale
z `scripts/test-locales.txt`; skrypt przygotowania środowiska generuje je
i sprawdza, czy działają.
Ten sam runner otacza target Valgrinda SC, więc Valgrind sprawdza sam plik
binarny testu. Suppressje obejmują opisane stosy systemowego GTK/fontconfig
oraz loader glibc podczas startu GTK; błędy aplikacji nadal przerywają kontrolę.
Uruchomione bezpośrednio bez ekranu testy paska postępu i karty Tests są
w CTest oznaczone jako pominięte.

`--skip-cppcheck`, `--skip-valgrind` i `--skip-clang-tidy` pozwalają pomijać
wybrane kontrole podczas pracy. Taki przebieg nie potwierdza przejścia pełnej
bramki. `-j<N>` określa równoległość kompilacji.

Bramka MISRA odrzuca aktywne znaleziska w zakresie współdzielonego VP37
i Adjustometera po uwzględnieniu udokumentowanych wyciszeń. Pozostała część
ECU ma raport analizy. Zobacz [zakres i zasady odstępstw](../MISRA.pl.md).

PMD CPD używa wersji wybranej przez HAL i progów 100 tokenów dla C/C++ oraz
50 dla Pythona. Kod produkcyjny jest porównywany wewnątrz modułu, a testy
hostowe i Python w całym repozytorium. Każda grupa duplikatów zatrzymuje
bramkę. Współdzielone helpery testów należą do [`src/common/tests/`](../src/common/tests/).

## Testy pojedynczego modułu

Przykład ECU, wykonywany z katalogu głównego:

```bash
cmake -S src/ECU -B src/ECU/build_test -DCMAKE_BUILD_TYPE=Release
cmake --build src/ECU/build_test --parallel
ctest --test-dir src/ECU/build_test --output-on-failure -LE static-analysis
```

Dla Clocks, Adjustometera lub VP37TestBench wybierz katalog
odpowiedniego modułu. Aplikację desktopową opisuje
[instrukcja SerialConfiguratora](../src/SerialConfigurator/README.pl.md#testy).
Listę zarejestrowanych testów pokaże `ctest --test-dir src/ECU/build_test -N`.
Samo wylistowanie nie uruchamia testów.

Same testy wyświetlacza i czasu Fiesta_clock uruchomisz poleceniem:

```bash
python3 -m unittest discover -s tests -p 'test_clock_*.py' -v
```

## Sprawdzenie aktualizacji HAL

Postępuj według [DEPENDENCIES.pl.md](../DEPENDENCIES.pl.md). Oprócz bramki
hostowej zbuduj release i debug **każdego modułu firmware** z `modules.json`,
w tym VP37TestBench, oraz sprawdź zadeklarowane warianty, takie jak ECU BENCH.
[Instrukcja kompilacji](BUILDING.pl.md#generowane-pliki-konfiguracji) podaje
kontrole rejestru i plików VS Code. Wybór modułów CI jest węższy od tej
lokalnej walidacji firmware.

## Zakres CI

| Workflow | Kontrole |
|---|---|
| [ecu-tests.yml](../.github/workflows/ecu-tests.yml) | Kompilacja ECU, testy, cppcheck, Valgrind i clang-tidy; testy Pythona z katalogu głównego, także po zmianach w modułach, które czytają. |
| [clocks-tests.yml](../.github/workflows/clocks-tests.yml), [adjustometer-tests.yml](../.github/workflows/adjustometer-tests.yml) | Kompilacja modułu, testy, Valgrind i clang-tidy. |
| [serial-configurator-tests.yml](../.github/workflows/serial-configurator-tests.yml) | Kompilacja GUI/CLI, testy, Valgrind i clang-tidy. |
| [firmware-build-scripts.yml](../.github/workflows/firmware-build-scripts.yml) | Release/debug modułów firmware wybranych do CI. |
| [windows-firmware.yml](../.github/workflows/windows-firmware.yml) | Kontrola generowania, buildy firmware i bazy komend kompilacji w Windows dla tego samego wyboru modułów. |
| [ecu-cppcheck.yml](../.github/workflows/ecu-cppcheck.yml) | Ręczne porównanie cppcheck z bazą ECU. |
| [ecu-misra.yml](../.github/workflows/ecu-misra.yml) | Ręczne wygenerowanie raportów MISRA. |

Filtry ścieżek w workflow wybierają przebiegi po zmianach modułu, kodu
współdzielonego, narzędzi lub wskaźnika HAL.
[`tests/test_module_registry.py`](../tests/test_module_registry.py) porównuje
listy modułów z rejestrem. Wgrywanie na urządzenie wymaga ręcznego sprawdzenia;
udana kompilacja nie potwierdza zachowania sprzętu.

[Codzienny runner systemd](../src/ECU/scripts/systemd/README.pl.md) przygotowuje
środowisko, testuje i buduje projekt w Linuksie, a wynik wysyła e-mailem.
Resetuje checkout do wybranej gałęzi zdalnej; nie używaj do tej usługi katalogu
z lokalnymi zmianami programistycznymi.

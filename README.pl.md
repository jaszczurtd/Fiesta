# Fiesta

Firmware i elektronika do Forda Fiesty 1.8 diesel. Projekt powstał dla
niestandardowego silnika łączącego elementy 1.8D z wtryskiem pośrednim oraz
1.8TDDI z wtryskiem bezpośrednim, z zamontowaną pompą VP37.

Projekt pozostaje na etapie POC. Cały system nie został jeszcze sprawdzony
jako zintegrowana całość w samochodzie drogowym. Próby ECU i VP37 odbywają się
na osobnym silniku zamontowanym na stalowej ramie w garażu. Część funkcji
pracowała już w samochodzie: rozszerzenie zegarów oraz ECU nadzorujące
parametry silnika bez sterowania VP37.

Autorem jest Marcin „Jaszczur" Kielesiński. Warstwę sprzętową i narzędzia
zapewnia [JaszczurHAL](https://github.com/jaszczurtd/JaszczurHAL).

## Moduły

- `ECU`: sterowanie silnikiem, pompą VP37, doładowaniem i odbiornikami,
  diagnostyka oraz zapis DTC.
- `Clocks`: fabryczne wskaźniki, dodatkowy ekran TFT i buzzer.
- `OilAndSpeed`: pomiar ciśnienia oleju, prędkości i temperatur spalin.
- `Adjustometer`: pomiar położenia nastawnika VP37 przez częstotliwość
  oscylatora, przekazywany do ECU przez I²C.
- `Fiesta_clock`: zegar RTC, publikacja czasu przez CAN i lokalny wyświetlacz.
- `SerialConfigurator`: aplikacja C/GTK4 oraz CLI do wykrywania modułów,
  odczytu i zmiany ustawień, aktualizacji firmware oraz podglądu GPS ECU.

Opis połączeń i odpowiedzialności zawiera anglojęzyczny
[ARCHITECTURE.md](ARCHITECTURE.md). Szczegóły pomiaru nastawnika opisuje
[README Adjustometera](src/Adjustometer/README.pl.md).

## Zależności

Wszystkie moduły korzystają z submodułu `src/JaszczurHAL`, przypiętego do
konkretnego commitu. Pobierz projekt razem z biblioteką:

```bash
git clone --recurse-submodules https://github.com/jaszczurtd/Fiesta.git
cd Fiesta
```

Po pobraniu zmian w istniejącym checkoutcie uruchom
`./scripts/init_hal_submodule.sh`. Skrypt wybiera zapisaną wersję HAL
i zatrzymuje się przy lokalnych zmianach w submodule. Zasady aktualizacji
opisuje [DEPENDENCIES.pl.md](DEPENDENCIES.pl.md).

Narzędzia potrzebne w Linux:

- podstawowe: `git`, `build-essential`, `cmake`, `ninja-build`, `python3`,
  `curl`, `ca-certificates`, `perl`;
- aplikacja desktopowa: `pkg-config`, `libgtk-4-dev`, `dpkg-dev`;
  opcjonalne `libshumate-dev` włącza mapę GPS zamiast pola zastępczego;
- kontrola jakości: `cppcheck`, `valgrind`, `clang-tidy`, `clang-tools`,
  `clang-format`; pakiet cppcheck dostarcza również dodatek MISRA;
- firmware RP: `gcc-arm-none-eabi`, `libstdc++-arm-none-eabi-newlib`,
  `libusb-1.0-0-dev` i `pkg-config`. HAL przygotowuje przypięte Pico SDK
  oraz `picotool`.

## Przygotowanie środowiska

Na Debianie i zgodnych systemach, z katalogu głównego projektu uruchom:

```bash
bash runmefirst.sh
```

Skrypt najpierw usuwa katalogi `build_test` i `.build` modułów firmware oraz
`build` SerialConfiguratora, także przy pomijaniu testów lub kompilacji.
Zachowuje katalogi kompilacji HAL i lokalne ustawienia VS Code.
Następnie instaluje pakiety, sprawdza Pythona, cppcheck z dodatkiem MISRA oraz
bibliotekę C++ toolchaina Arm. Następnie inicjalizuje przypięty submoduł HAL,
przygotowuje jego zależności, uruchamia testy hostowe i analizatory, kompiluje
pięć modułów firmware, przygotowuje artefakty UF2 z manifestami oraz buduje,
testuje i pakuje SerialConfigurator do pakietu Debian.

Uruchamiaj go jako zwykły użytkownik. `sudo` jest używane tylko do instalacji
pakietów; uruchomienie całego skryptu jako root utworzyłoby pliki robocze
należące do roota. Skrypt odrzuca taki sposób wywołania, chyba że jawnie
ustawisz `ALLOW_ROOT=1`.

Opcje środowiskowe: `SKIP_APT=1`, `APT_NONINTERACTIVE=1`, `SKIP_TESTS=1`,
`SKIP_BUILD=1`, `SKIP_DESKTOP=1`, `SKIP_DESKTOP_PACKAGE=1`.

## Środowiska pracy

Linux, w tym Raspberry Pi OS i systemy zgodne z Debianem, jest głównym
środowiskiem rozwoju. WSL2 obsługuje te same skrypty, lecz upload wymaga
udostępnienia rzeczywistego urządzenia USB lub dysku BOOTSEL.

Windows obsługuje rozwój wszystkich pięciu firmware. Po uruchomieniu
`src/JaszczurHAL/runmefirst.ps1` zadania VS Code pozwalają kompilować release
i debug, odświeżać IntelliSense, wgrywać firmware i otwierać monitor portu.
Testy hostowe, cppcheck, MISRA, Valgrind i SerialConfigurator pozostają
zorientowane na Linux. macOS nie był sprawdzany.

Każdy moduł firmware ma katalog `.vscode/`. Wybór płytki i portu trafia do
ignorowanego `jaszczurhal.local.json`; repozytorium nie zapisuje numerów COM.
Pliki zadań generuje `scripts/sync_vscode_projects.py`, korzystając z rejestru
HAL. Hook pre-commit regeneruje i dodaje zmienione pliki do indeksu.
`python3 scripts/sync_vscode_projects.py --check` sprawdza ich aktualność.
Hooki można skonfigurować przez `scripts/configure_git_hooks.py`.

Workflow Windows kompiluje wszystkie moduły i odświeża bazy komend
kompilacji. Weryfikacja uploadu na fizycznym urządzeniu pozostaje ręczna.

## Kompilacja i testy

Aplikacje używają punktów wejścia `app_start()` oraz `app_task0()` z HAL,
a przy drugim kontekście wykonania również `app_task1()`.

Zestaw testów hostowych, cppcheck, Valgrind i clang-tidy uruchomisz przez:

```bash
./runalltests.sh -j8
```

Obsługiwane opcje pomijania analizatorów to `--skip-cppcheck`,
`--skip-valgrind` i `--skip-clang-tidy`. CTest uruchamia testy wykonania
z wyłączeniem etykiety `static-analysis`; analizatory mają osobne etapy.

Dla ECU, Clocks, OilAndSpeed i Adjustometera można uruchomić testy osobno:

```bash
cmake -S src/<Module> -B src/<Module>/build_test -DCMAKE_BUILD_TYPE=Release
cmake --build src/<Module>/build_test --parallel
ctest --test-dir src/<Module>/build_test --output-on-failure
```

Fiesta_clock jest sprawdzany przez kompilację firmware; nie ma własnego
projektu testów hostowych. SerialConfigurator ma osobny projekt CMake.

W katalogu wybranego modułu firmware:

```bash
../JaszczurHAL/vscode/entry/jh-vscode build --project "$PWD"
../JaszczurHAL/vscode/entry/jh-vscode build-debug --project "$PWD"
../JaszczurHAL/vscode/entry/jh-vscode upload --project "$PWD"
../JaszczurHAL/vscode/entry/jh-vscode upload-uf2 --project "$PWD"
../JaszczurHAL/vscode/entry/jh-vscode refresh-intellisense --project "$PWD"
```

`Project: Upload` (`Ctrl+Shift+2`) używa uploadu przez USB CDC i weryfikuje
tożsamość modułu. Wariant UF2 korzysta z dysku BOOTSEL. Monitor portu
(`Ctrl+Shift+3`) odnajduje moduły po identyfikatorach
`/dev/serial/by-id/usb-Jaszczur_Fiesta_*`.

SerialConfigurator:

```bash
cd src/SerialConfigurator
./scripts/desktop-build.sh build
./scripts/desktop-build.sh run
./scripts/desktop-build.sh test
./build/serial-configurator-cli detect
```

GUI i CLI współdzielą obsługę protokołu. CLI udostępnia `detect`, `list`,
`meta`, `param-list`, `get-values`, `get-param`, `get-gps`,
`reboot-bootloader`, `set-param`, `commit-params`, `revert-params`
i `set-and-commit`. Zapisy parametrów wymagają uwierzytelnienia. GUI
sprawdza format UF2 i manifest przed aktualizacją, obsługuje restart do
BOOTSEL, kopiowanie z postępem i ponowne wykrycie modułu.

## Debugowanie i automatyzacja

Konfiguracje Cortex-Debug obsługują Raspberry Pi Debug Probe w trybie
CMSIS-DAP, z firmware v2 lub nowszym, oraz starszy Picoprobe. Połącz
`SC` z `SWCLK`, `SD` z `SWDIO` oraz masy; płytka wymaga własnego zasilania.
Potrzebne są rozszerzenie `marus25.cortex-debug`, OpenOCD i GDB dla Arm.

W VS Code naciśnij F5 i wybierz RP2040, RP2350 albo dołączenie do działającego
układu. Wariant launch buduje debug przed wgraniem; attach nie wgrywa obrazu.
Upload przez `jh-vscode` korzysta z USB, a nie z sondy SWD.

W `src/ECU/scripts/systemd/` znajdują się usługa i timer użytkownika,
które codziennie o 13:00 pobierają projekt, przygotowują środowisko, budują
firmware i wysyłają wynik e-mailem. Konfigurację, wymagania sudo i SMTP
opisuje znajdujący się tam anglojęzyczny README. Runner korzysta z bootstrapu
Fiesty i przypiętej wersji HAL.

## Zakres walidacji

Projekt nie deklaruje zgodności z ISO 26262, AUTOSAR ani pełnej zgodności
MISRA. ECU jest objęte migracją MISRA-C; jej stan i odchylenia opisuje
anglojęzyczny [MISRA.md](MISRA.md). Pozostałe moduły nie są objęte tym
zakresem. Testy hostowe używają mocków HAL; projekt nie ma stanowiska HIL.
Rozwój prowadzi jedna osoba, z kontrolą przez testy i CI.

RP2040 nie jest układem klasy AEC-Q100. Wejście Halla silnika korzysta
z przerwań GPIO; Adjustometer mierzy częstotliwość przez PIO/DMA.
Bezpieczeństwo działania ECU, defensywna obsługa błędów oraz dalsza
walidacja sprzętowa pozostają istotną częścią prac.

Zdjęcia elektroniki, silnika testowego, konfiguratora i diagnostyki są
w `materials/imgs/` oraz w [galerii projektu](https://postimg.cc/gallery/pHF4jy2).

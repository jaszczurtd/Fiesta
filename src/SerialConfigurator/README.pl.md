# SerialConfigurator

[English](README.md) · [O Fieście](../../README.pl.md) · [Protokół](../common/scDefinitions/PROTOCOL.pl.md)

SerialConfigurator to aplikacja C/GTK4 dla Linuksa do wykrywania modułów USB,
odczytu i zmiany ustawień, wgrywania firmware i testów stanowiskowych ECU.
CLI używa tej samej biblioteki do wykrywania, ustawień, GPS, testów
oraz uwierzytelnionego wejścia do bootloadera. Pełna aktualizacja firmware
odbywa się w GUI.

## Kompilacja

Pracuj w `src/SerialConfigurator`. W Linuksie zgodnym z Debianem zainstaluj:

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libgtk-4-dev dpkg-dev
```

`libshumate-dev` włącza mapę OpenStreetMap; bez niego karta mapy wyświetla
informację zastępczą. `valgrind`, `clang-tidy` i `clang-tools` umożliwiają
lokalne kontrole jakości. Instaluje je również [setup Fiesty](../../doc/BUILDING.pl.md).

```bash
cmake -S . -B build
cmake --build build
./build/serial-configurator
```

CMake używa `SC_JASZCZURHAL_DIR=../JaszczurHAL`, czyli submodułu Fiesty.
Osobny checkout wskaż przez `-DSC_JASZCZURHAL_DIR=/absolute/path/to/JaszczurHAL`.
Build wymaga kodeka ramek i źródeł kryptografii/uwierzytelniania HAL;
ich brak zatrzymuje konfigurację. Kompiluje `hal_serial_frame.h`,
`hal_crypto.cpp` i `hal_sc_auth.cpp` bez lokalnych kopii implementacji.
Dwa ostatnie zapewniają Base64, MD5, SHA-256/HMAC-SHA256, ChaCha20-Poly1305
i to samo uwierzytelnianie przez UID co firmware, przez `sc_crypto.h` i `sc_auth.h`.

## Wykrywanie i wybór modułu

Kliknij **Detect Fiesta Modules**. Skan w tle wysyła `HELLO` do urządzeń
`/dev/serial/by-id/usb-Jaszczur_Fiesta_*`, odczytuje metadane i katalogi
parametrów oraz sprawdza każdy `SC_GET_PARAM` względem zakresu i wartości
domyślnej. Okno pokazuje `Detecting...`. Poprawna odpowiedź zmienia wskaźnik
modułu na zielony; log pokazuje szczegóły. Skan sprawdza wszystkich kandydatów,
aby zgłosić niejednoznaczny wybór przy powtórzonych modułach.
**Disconnect** usuwa wykryty stan i ustawia wskaźniki na czerwono.

Rejestr wybiera do protokołu ECU, Clocks, OilAndSpeed i RTC_Clock.
Adjustometer i VP37TestBench nie są obsługiwane przez konfigurator.
Odczyt nie wymaga uwierzytelnienia:

```bash
./build/serial-configurator-cli detect
./build/serial-configurator-cli list
./build/serial-configurator-cli meta --module ECU
./build/serial-configurator-cli param-list --module ECU
./build/serial-configurator-cli get-values --uid E661A4D1234567AB
./build/serial-configurator-cli get-param nominal_rpm --module ECU
```

UID w przykładzie zastąp odczytanym z własnej płytki. Wybierz urządzenie
przez `--module`, `--uid` lub `--port`. Niejednoznaczny wybór jest odrzucany.
Odpowiedzi parametrów pokazują rozpoznane typy `BOOL`, `INT`, `UINT`, `FLOAT` lub `TEXT`.

## Zmiana ustawień

ECU udostępnia zapisywane progi w wersjonowanym bloku KV. RTC_Clock przyjmuje
`rtc_year/month/day/hour/minute/second` w czasie CET/CEST i zapisuje je w RTC
jako UTC. Pole `rtc_integrity` tylko do odczytu ma wartość 1 wyłącznie wtedy,
gdy RTC zawiera zaufany czas UTC. Commit sprawdza pełną datę, w tym lata
przestępne. Clocks i OilAndSpeed
pokazują progi oraz interwały tylko do odczytu, bez utrwalania.

Na karcie Values edytuj pola, wybierz **Apply staged**, a potem **Commit**,
aby sprawdzić i zastosować zmiany. **Revert** przywraca kopię roboczą
z aktywnych wartości. Zapis wymaga uwierzytelnienia. CLI obsługuje te same kroki:

```bash
./build/serial-configurator-cli set-param --id nominal_rpm --value 900 --module ECU
./build/serial-configurator-cli commit-params --module ECU
./build/serial-configurator-cli revert-params --module ECU
```

Pojedynczą zmianę w jednej uwierzytelnionej sesji wykonasz tak:

```bash
./build/serial-configurator-cli set-and-commit --id nominal_rpm --value 900 --module ECU
```

Po błędzie commita `set-and-commit` próbuje wycofać kopię roboczą.
[Opis protokołu](../common/scDefinitions/PROTOCOL.pl.md#parametry) wyjaśnia
schematy zapisu i rozszerzanie katalogu bez utraty zapisanych wartości.

## Wgrywanie firmware

Przygotuj UF2 i jego [manifest](../../doc/BUILDING.pl.md#manifest-dla-konfiguratora).
Sekcja każdego modułu w GUI pozwala wybrać obraz i opcjonalny manifest,
zapamiętuje ścieżki w `flash-paths.json` i pokazuje postęp. Okno wyboru
zaczyna od zapamiętanego katalogu, potem próbuje katalogu drugiego pola
wyboru, a następnie `./<Module>/.build`, jeśli jest dostępny.

Manifest wymaga `module_name`, `fw_version`, `build_id` i `sha256`.
Opcjonalne `uf2_file` pozwala odnaleźć obraz obok manifestu przy wyborze
samego manifestu; musi zawierać nazwę bez ścieżki. `signature` jest
odczytywane, ale nie weryfikowane: obsługa ed25519 nie została wdrożona.

Aktualizacja wykonuje kolejno:

1. Sprawdza format UF2, manifest, tożsamość modułu i SHA-256.
2. Uwierzytelnia sesję przez `HELLO`, `SC_AUTH_BEGIN` i `SC_AUTH_PROVE`, używając HMAC-SHA256 i klucza z UID. Wyzwanie jest jednorazowe; nowe `HELLO` usuwa uwierzytelnienie.
3. Wysyła `SC_REBOOT_BOOTLOADER` i czeka na potwierdzenie.
4. Wyszukuje dysk BOOTSEL w `/media/$USER` lub `/run/media/$USER` (`RPI-RP2*` albo `RP2350`).
5. Kopiuje UF2 porcjami, po czym czeka na ten sam UID w `/dev/serial/by-id/`.
6. Ponawia `HELLO` i, jeśli operacja tego wymaga, porównuje wersję firmware z manifestem.

Pasek postępu pulsuje podczas oczekiwania i pokazuje ułamek przy kopiowaniu.
W trakcie aktualizacji wykrywanie, przyciski Flash innych modułów i pola
wyboru plików są nieaktywne. `ScFlashStatus` rozróżnia błędy, np.
`MANIFEST_MODULE_MISMATCH`, `BOOTSEL_TIMEOUT` i `POST_FLASH_FW_MISMATCH`.

CLI może uwierzytelnić sesję i zażądać wejścia do bootloadera:

```bash
./build/serial-configurator-cli reboot-bootloader --module ECU \
  --manifest /path/to/firmware.manifest.json
```

To polecenie nie wykonuje kopiowania UF2 i ponownego połączenia z sekwencji GUI.

## Odczyt GPS

```bash
./build/serial-configurator-cli get-gps --module ECU
```

ECU raportuje dostępność, szerokość i długość geograficzną, prędkość oraz
czas UTC przez `SC_GET_GPS`. Karta GPS View odpytuje co 4 s. Z libshumate
pokazuje znacznik OpenStreetMap i centrowanie; bez niego informację zastępczą.

## Testy stanowiskowe ECU

Strona ECU na karcie Values zawiera Settings i Tests. `SC_TEST_LIST`
dostarcza testy z opisami, parametrami, Defaults i Run. **Run sequence**
uruchamia testy należące do sekwencji; **Skip** i **Stop** działają na
bieżący test. Firmware bez testów lub poleceń pokazuje **No tests available**.

Run uwierzytelnia sesję, wysyła edytowane parametry i uruchamia test.
Parametry obowiązują do resetu ECU. Panel statusu pokazuje test, inicjatora,
krok sekwencji, czas, zadane i zmierzone położenie, postęp i ostatni wynik.
Odpytuje co 250 ms podczas swojego testu, również gdy użytkownik ogląda inną
kartę. ECU zatrzymuje test po końcu sesji albo po 3 s bez poleceń testowych.
Zamknięcie okna, rozłączenie lub nowe wykrywanie kończy więc test. Domyślna
blokada RPM odrzuca lub zatrzymuje test powyżej skonfigurowanej granicy obrotów.

```bash
./build/serial-configurator-cli test-list --module ECU
./build/serial-configurator-cli test-set --id cyclic_passes --value 2 --module ECU
./build/serial-configurator-cli test-run all --module ECU
./build/serial-configurator-cli test-status --module ECU
./build/serial-configurator-cli test-skip --module ECU
./build/serial-configurator-cli test-stop --module ECU
```

`test-run` pokazuje postęp co sekundę; Ctrl-C prosi o zatrzymanie. Kod wyjścia
wynosi 0, gdy wszystkie zakończone testy zwracają `done` lub `ok`, a w innych
przypadkach 6. [Zasady testów](../common/scDefinitions/PROTOCOL.pl.md#testy-funkcjonalne)
opisują blokady, testy dostępne tylko w konsoli i świeżość statusu.

## Testy

```bash
./scripts/desktop-build.sh test
ctest --test-dir build -N
```

Pierwsze polecenie buduje i uruchamia testy; drugie tylko wyświetla listę.
Zakres obejmuje wybór urządzenia, wykrywanie, ramki i odpowiedzi,
uwierzytelnianie/kryptografię, manifesty, transakcje parametrów, GPS, testy
funkcjonalne, terminy odpowiedzi i odtwarzanie sesji po `HELLO_REQUIRED`.
Testy wgrywania używają mocka transportu i tymczasowych katalogów BOOTSEL/by-id;
nie potwierdzają wgrywania przez fizyczne USB. GTK dodaje testy paska postępu
i karty Tests; ten ostatni jest pomijany bez ekranu.

CMake utrzymuje listę testów, w tym `serial-configurator-cli-selectors-tests`.
Zachowuj `-Wall -Wextra -Werror`. [Workflow CI](../../.github/workflows/serial-configurator-tests.yml)
buduje GUI/CLI, uruchamia testy, Valgrind i clang-tidy.

## Pakiet i edytor

```bash
./scripts/desktop-build.sh package
```

Pakiet trafia do `build/*.deb`. Instalacja dodaje **Fiesta USB Configurator**
do menu aplikacji z ikoną `scripts/icon.png`.

| Skrót | Zadanie VS Code | Działanie |
|---|---|---|
| Ctrl+Shift+1 | `Project: Build` | Konfiguracja i kompilacja. |
| Ctrl+Shift+2 | `Project: Upload` | Kompilacja i uruchomienie aplikacji. |
| Ctrl+Shift+5 | `Project: Test` | Kompilacja i CTest. |
| Ctrl+Shift+6 | `Project: Refresh IntelliSense` | Konfiguracja z `CMAKE_EXPORT_COMPILE_COMMANDS=ON` i odtworzenie `c_cpp_properties.json`. |
| Ctrl+Shift+7 | `Project: Clean` | Usunięcie `build/`. |

Skopiuj wpisy z `.vscode/keybindings.reference.json` do skrótów użytkownika
VS Code (`~/.config/Code/User/keybindings.json` w Linuksie).

## Mapa kodu

`src/ui/main.c` wywołuje `sc_app_run()`, a `sc_app.c` tworzy okno.
`sc_detection.c` wykrywa urządzenia w tle, `sc_module_details.c` rysuje
szczegóły, a `sc_flash_tab.c`, `sc_values_tab.c`, `sc_map_tab.c`
i `sc_tests_tab.c` implementują odpowiednie widoki.

`src/core/sc_core.c` koordynuje wykrywanie i sesje. `sc_transport.c` obsługuje
porty POSIX. `sc_flash.c`, `sc_manifest.c`, `sc_gps.c` i `sc_tests.c`
implementują odpowiednie operacje, a `sc_text.c` i `sc_time.c` współdzielone
funkcje tekstowe i czasowe. `src/cli/sc_cli_main.c` kieruje polecenia do
sąsiednich modułów obsługi poleceń, wyboru urządzenia i formatowania wyników.

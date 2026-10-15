# Przygotowanie środowiska i kompilacja firmware

[English](BUILDING.md) · [O projekcie](../README.pl.md) · [Testy i CI](TESTING.pl.md)

Polecenia wykonuj z katalogu głównego Fiesty, chyba że rozdział wskazuje inaczej.
Firmware używa funkcji wejściowych HAL `app_start()` i `app_task0()`,
a dla drugiego kontekstu wykonania także `app_task1()`.

## Przygotowanie Linuksa lub WSL

Pobierz projekt z submodułami według [wprowadzenia](../README.pl.md#pierwsze-kroki).
Na Linuksie zgodnym z Debianem lub Raspberry Pi OS uruchom jako zwykły użytkownik:

```bash
bash runmefirst.sh
```

Skrypt **przed przygotowaniem środowiska** usuwa katalogi `build_test` i `.build`
wszystkich modułów firmware oraz `build` SerialConfiguratora, także przy
pomijaniu kompilacji lub testów. Zachowuje katalogi kompilacji HAL, źródła
i lokalne ustawienia VS Code. Następnie:

1. Instaluje pakiety apt i sprawdza Python 3 oraz bibliotekę uruchomieniową C++ dla Arm.
2. Inicjalizuje commit HAL zapisany w Fieście; lokalne zmiany HAL zatrzymują ten krok.
3. Przygotowuje zależności HAL, Pico SDK, picotool, PMD i wybraną przez HAL wersję cppcheck z dodatkiem MISRA.
4. Uruchamia [kontrole hostowe](TESTING.pl.md).
5. Buduje każdy moduł firmware i tworzy sprawdzony `firmware.manifest.json` obok `firmware.uf2`.
6. Buduje i testuje SerialConfigurator oraz tworzy pakiet Debiana, jeśli nie wyłączono tego kroku.

Lista pakietów znajduje się w
[`ensure-system-dependencies.sh`](../src/ECU/scripts/ensure-system-dependencies.sh).
Obejmuje toolchainy C/C++ i Arm, CMake, Ninja, Python, Git, curl, certyfikaty CA,
Perl, libusb, pkg-config, GTK4, dpkg-dev, libshumate, Valgrind, clang-format,
clang-tidy, clang-tools i Javę dla PMD. Setup buduje cppcheck w wersji wybranej
przez HAL; wersje z dystrybucji mogą dawać inne wyniki analizy.

Skrypt używa `sudo` do apt. Uruchomienie całości jako root jest odrzucane,
ponieważ pozostawia katalogi kompilacji należące do roota. `ALLOW_ROOT=1`
jawnie wyłącza to sprawdzenie. Pozostałe opcje środowiskowe:

| Opcja | Działanie |
|---|---|
| `SKIP_APT=1` | Pomija instalację pakietów systemowych. |
| `APT_NONINTERACTIVE=1` | Wymaga instalacji apt przez sudo bez pytania o hasło. |
| `SKIP_TESTS=1` | Pomija krok testów hostowych bootstrapu. |
| `SKIP_BUILD=1` | Pomija kompilację firmware. |
| `SKIP_DESKTOP=1` | Pomija osobny krok kompilacji i pakowania aplikacji desktopowej. |
| `SKIP_DESKTOP_PACKAGE=1` | Buduje aplikację desktopową bez pakietu Debiana. |

Opcje nie wyłączają początkowego czyszczenia. Kontrole hostowe same obejmują
SerialConfigurator; `SKIP_DESKTOP` nie filtruje ich listy. Uruchamianie bez
nadzoru opisuje [instrukcja systemd](../src/ECU/scripts/systemd/README.pl.md).

## Środowiska programistyczne

- **Linux** jest podstawowym środowiskiem firmware, aplikacji desktopowej i kontroli jakości.
- **WSL2** korzysta z procedur linuksowych, ale wgrywanie wymaga dostępu do USB
  lub dysku BOOTSEL. Jeśli WSL go nie widzi, użyj powłoki po stronie Windows.
- **Windows** obsługuje rozwój firmware modułów z rejestru. Uruchom
  `src/JaszczurHAL/runmefirst.ps1`, a następnie zadania VS Code lub
  `src/JaszczurHAL/vscode/entry/jh-vscode.cmd`. Testy hostowe, cppcheck, MISRA,
  Valgrind i aplikacja desktopowa pozostają przeznaczone głównie dla Linuksa.
- **macOS** nie został sprawdzony. Nie ma potwierdzonej pełnej procedury
  przygotowania środowiska i pracy ze sprzętem.

## Kompilacja modułu

Przykład jawnie wybiera ECU i montaż `rp2040-plus`:

```bash
src/JaszczurHAL/vscode/entry/jh-vscode build --project src/ECU --assembly rp2040-plus
src/JaszczurHAL/vscode/entry/jh-vscode build-debug --project src/ECU --assembly rp2040-plus
src/JaszczurHAL/vscode/entry/jh-vscode refresh-intellisense --project src/ECU --assembly rp2040-plus
```

Dla innego modułu wybierz montaż z jego `device_tree.json`. Ustawienia firmware
i tożsamość USB pochodzą z `.vscode/jaszczurhal.project.json`, a lokalny wybór
jest zapisywany w ignorowanym `.vscode/jaszczurhal.local.json`. Przy jednym
montażu narzędzie może wybrać go automatycznie. Wynik rozwiązywania konfiguracji
można odczytać bez kompilacji:

```bash
src/JaszczurHAL/vscode/entry/jh-vscode config-dump --project src/ECU --assembly rp2040-plus --json
```

Pliki `firmware.elf` i `firmware.uf2` trafiają do
`src/<Module>/.build/<target>/<assembly>/`. ECU używa
`src/ECU/.build/rp2040/rp2040-plus/`. Obraz stanowiskowy zbudujesz poleceniem:

```bash
src/JaszczurHAL/vscode/entry/jh-vscode build --project src/ECU --assembly rp2040-plus --variant BENCH
```

Ten obraz trafia do podkatalogu `variants/BENCH/` w katalogu montażu.
Jego testy funkcjonalne opisuje [dokumentacja ECU](../src/ECU/README.pl.md#obraz-stanowiskowy-i-sprzęt).
Zmiana montażu lub wariantu zmienia ścieżkę plików wynikowych; przed wyborem
obrazu do wgrania sprawdź ją przez `config-dump`.

## Wgrywanie i monitor portu

Te polecenia działają na podłączonym sprzęcie:

```bash
src/JaszczurHAL/vscode/entry/jh-vscode upload --project src/ECU --assembly rp2040-plus
src/JaszczurHAL/vscode/entry/jh-vscode upload-uf2 --project src/ECU --assembly rp2040-plus
src/JaszczurHAL/vscode/entry/jh-vscode monitor --project src/ECU
```

`upload` sprawdza tożsamość USB przed wgraniem, a `upload-uf2` używa dysku
BOOTSEL. Monitor wybiera moduł według tożsamości USB, w Linuksie również przez
stałe ścieżki `/dev/serial/by-id/usb-Jaszczur_Fiesta_*`. Odpowiadają im zadania
VS Code `Project: Upload`, `Project: Upload (UF2 / BOOTSEL)` i
`Project: Serial Monitor`. Wzorcowe skróty to Ctrl+Shift+2 dla wgrywania
oraz Ctrl+Shift+3 dla monitora; skopiuj definicje modułu do ustawień skrótów użytkownika.

## Manifest dla konfiguratora

Zwykłe `jh-vscode build` tworzy pliki firmware. Bootstrap Fiesty dodatkowo
generuje i sprawdza manifest dla SerialConfiguratora. Po bezpośredniej kompilacji
przygotuj go jawnie dla wybranego UF2; ponów ten krok po każdej zmianie obrazu.
Przykład dla domyślnego obrazu ECU:

```bash
(
    set -e
    source src/common/scripts/fiesta-firmware-common.sh
    fiesta_prepare_manifest_for_uf2 "$PWD/src/ECU" \
        "$PWD/src/ECU/.build/rp2040/rp2040-plus/firmware.uf2"
)
```

Helper zapisuje `firmware.manifest.json` obok UF2 i sprawdza moduł, nazwę pliku
oraz SHA-256. Odczytuje wersję i identyfikator buildu z `config.h` modułu;
gdy nie są zdefiniowane jako literały, używa wartości domyślnych.
[Protokół](../src/common/scDefinitions/PROTOCOL.pl.md#wgrywanie-firmware)
opisuje wymagane pola i opcjonalny, nieweryfikowany podpis. Przygotowanie
manifestu jest osobnym krokiem względem kompilacji i sprawdzania tożsamości USB.

## Debugowanie przez SWD

Podłącz sondę Raspberry Pi Debug Probe z CMSIS-DAP: `SC` do `SWCLK`, `SD` do
`SWDIO` oraz wspólną masę. Zapewnij zasilanie zgodnie z wymaganiami płytki.
Zainstaluj Cortex-Debug (`marus25.cortex-debug`) i udostępnij mu `openocd`
oraz `arm-none-eabi-gdb`. Setup HAL dla Windows ustawia ścieżki narzędzi
w profilu użytkownika VS Code; na innych systemach użyj PATH lub ustawień Cortex-Debug.

Otwórz katalog modułu, naciśnij F5 i wybierz profil montażu. ECU udostępnia
`Project: Debug rp2040-plus (RP2040)` i jego odpowiednik `variant: BENCH`.
Każdy buduje wersję debug, wgrywa ją i zatrzymuje wykonanie w `main`.
Pozostałe moduły mają profile dla własnych montaży RP2040. Generowane pliki
obecnie nie zawierają profili RP2350 ani samego dołączenia do działającego układu.
Wgrywanie przez USB obsługuje `jh-vscode`; sonda służy do pracy Cortex-Debug/GDB.

## Generowane pliki konfiguracji

[`modules.json`](../modules.json) zawiera rodzaje modułów, tokeny, testy hostowe
i wybór modułów CI. `scripts/fiesta_modules.py` generuje nagłówek tokenów,
tabelę SerialConfiguratora i listy dla skryptów powłoki. Generator VS Code
czyta ten rejestr, rejestry HAL i montaże. Sprawdź oba z katalogu głównego:

```bash
python3 scripts/fiesta_modules.py --check
python3 scripts/sync_vscode_projects.py --check
```

Uruchomienie generatora bez `--check` odtwarza jego pliki wynikowe. Lokalny
hook pre-commit uruchamia generator VS Code z `--stage` i dodaje zarządzane
pliki do indeksu. `scripts/configure_git_hooks.py` konfiguruje ścieżkę hooka
w Linuksie, macOS i Windows. Zmiany generatora utrzymuj razem z wymaganą przez
nie wersją HAL; zobacz [aktualizację zależności](../DEPENDENCIES.pl.md).

SerialConfigurator ma osobną [instrukcję kompilacji](../src/SerialConfigurator/README.pl.md#kompilacja).

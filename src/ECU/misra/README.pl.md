# Analiza MISRA ECU

[English](README.md) · [Zakres i zasady](../../../MISRA.pl.md)

Runner analizuje ECU lub Adjustometer za pomocą wersji cppcheck wybranej
przez HAL. Generuje znaleziska do przeglądu i uwzględnia przyjęte odstępstwa.
Licencjonowane teksty reguł MISRA dostarcza się lokalnie; nie są dystrybuowane tutaj.

## Pliki

- `check_misra.sh` uruchamia cppcheck i dodatek `misra.py` przez
  `scripts/cppcheck.sh` w HAL. Wersja jest określona przez HAL, ponieważ
  wyniki i dodatek zmieniają się między wydaniami. `--cppcheck` albo
  `CPPCHECK_BIN` pozwalają wybrać inny plik wykonywalny. Runner ładuje model
  atomików z sąsiedniego checkoutu HAL, gdy jest dostępny, i domyślnie
  zapisuje raporty w `misra/.results/`.
- `suppressions.txt` zawiera wyciszenia potwierdzonych fałszywych alarmów
  lub przyjętych odstępstw. Każde musi mieć odpowiednik w rejestrze.
- [deviation-register.pl.md](deviation-register.pl.md) opisuje odstępstwa,
  fałszywe alarmy i znaleziska oczekujące na decyzję.

## Uruchamianie analizy

Z katalogu głównego repozytorium:

```bash
cd src/ECU
bash misra/check_misra.sh --out misra/.results
```

## Licencjonowane teksty reguł

Repozytorium nie dostarcza tekstów MISRA. Jeśli masz lokalny, licencjonowany
wyciąg Appendix A, wskaż go przez `--rule-texts /absolute/path/to/file`.
Runner wykrywa też `src/ECU/misra/rule-texts.local.txt`; Git ignoruje ten plik.

## Pliki wynikowe

- `.results/results.txt`: pełny wynik cppcheck.
- `.results/active-findings.txt`: znaleziska MISRA wybrane z pełnego wyniku.
- `.results/rule-counts.txt`: liczba aktywnych znalezisk dla każdej reguły.
- `.results/error_count.txt`: łączna liczba aktywnych znalezisk.
- `.results/summary.txt`: podsumowanie skanu.
- `.results/severity-counts.txt`: podział według kategorii, dostępny tylko
  po dostarczeniu licencjonowanych tekstów reguł.

## Przegląd

Najpierw popraw kod. Wyciszenia stosuj wyłącznie do potwierdzonych fałszywych
alarmów lub jawnie przyjętych odstępstw. Rejestr i `suppressions.txt` muszą
pozostawać zgodne. Raport wspiera migrację ECU do MISRA; sam nie dowodzi zgodności.

## Konfiguracja i porównania

Runner wymusza `hal_project_config.h` w każdej jednostce translacji, wybiera
gałąź GNU w `hal_compiler.h` i ładuje model atomików HAL. Dzięki temu cppcheck
widzi funkcje włączane konfiguracją i operacje atomowe.
`--project` rozwiązuje ścieżkę względem bieżącego katalogu. Z katalogu głównego
wybierz `--project src/Adjustometer`, a z `src/ECU` użyj
`--project ../Adjustometer`. `--fail-paths` odrzuca przebieg z aktywnymi
znaleziskami w pasujących ścieżkach. Pełne opcje pokazuje `bash misra/check_misra.sh --help`.

Zachowuj wyniki razem z rewizjami Fiesty/HAL, wersją narzędzia i opcjami skanu.
[Historia](history.pl.md) wyjaśnia, dlaczego dawnych analiz z wyłączonymi
funkcjami HAL nie można porównywać bezpośrednio. Bieżący zakres i kryteria
są utrzymywane w [zasadach projektu](../../../MISRA.pl.md).

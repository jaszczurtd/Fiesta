# Historyczne wyniki analizy MISRA

[English](history.md) · [Bieżący zakres i zasady](../../../MISRA.pl.md)

Te notatki zachowują wcześniejsze porównania. Nie opisują checkoutu bez nowego
skanu. Wpisy z 30 września dotyczą kolejnych stanów tego samego dnia; ostatnia
zapisana liczba dla ECU to 861, a nie wcześniejsze 1023. Nie dołączono tutaj
dokładnego commita ani archiwum wyniku dla ostatniej liczby, więc nie jest ona
odtwarzalną bazą bieżącego stanu. Wynik 949 pochodzi z 12 września.
Kolejne porównania wymagają nowych raportów i metadanych rewizji.

## Porównania z września 2026

Skan z 2026-09-30, cppcheck 2.13.0, zgłosił **1023 aktywne znaleziska**
dla 31 reguł: 709 w `src/ECU` i 314 w `src/common`.

Wcześniejszy wynik 1044 na `0b3e84f` zmalał o 21 po oddzieleniu VP37 od
pozostałego ECU: callbacków, odczytu Adjustometera, atomowej kopii stanu
i funkcji strojenia. Porównanie według pliku i reguły pokazało przeniesienie
znalezisk odczytu Adjustometera z `sensors.c` do `vp37_adjustometer.c`, a stałych
z `vp37.h` do `vp37_config.h`. Doszły dwa znaleziska Advisory 8.7 dla
`VP37_snapshotBegin/End` z `TESTABLE_STATIC`, tak jak dla ośmiu funkcji
w `sensors.c` (runner definiuje `UNIT_TEST`), jedno 15.5 dla wcześniejszego
wyjścia z `VP37_init` oraz dwa 2.5 dla makr używanych tylko przez telemetrię
stanowiskową: `VP37_PUBLICATION_POLL_US` i `START_VP37_REPORT_WAIT_US`.

Stan jest współdzielony między rdzeniami wyłącznie jako słowa odczytywane
i zapisywane atomowo. Konwersję floatów i struktury telemetrii przez unie
zapisano jako odstępstwo DR-007 od Advisory 19.2. Porządki relaxed, acquire
i release w publikacji, przekazywaniu bufora i warstwie testów opisuje DR-008
względem reguły 21.25 z Amendment 4, której cppcheck 2.13 nie sprawdza.

Przeniesienie VP37 do `src/common/vp37` tego samego dnia pozostawiło **1023**.
Runner MISRA, cppcheck i clang-tidy objęły nowy katalog; porównanie według
pliku i reguły wykazało jedynie zmianę prefiksu ścieżki. Wyciszenia 8.7 z DR-005
zostały dostosowane do nowych ścieżek.

Później 2026-09-30, w kroku 4 wydzielenia modułu, VP37 i Adjustometer objęto
wymaganiami MISRA. Zapisano **zero aktywnych znalezisk** w VP37,
współdzielonych nagłówkach protokołu i Adjustometerze, kontrolowane przez
bramkę 4 `runalltests.sh`: `--fail-paths` i drugi skan
z `--project src/Adjustometer`. Znalezisk Mandatory nie było. Required
oraz Advisory poprawiono z wyjątkami: 15.5 jako DR-009, kontekst
`TESTABLE_STATIC` jako DR-010, mapa rejestrów jako DR-011, zależne od
konfiguracji makra nagłówków jako DR-012, makra buildsystemu jako DR-013
i generowany rejestr tokenów jako DR-014. Reguła 21.25 pozostała w DR-008.

Zapisany wynik całego ECU to **861**: 590 w `src/ECU` i 271 w `src/common`.
Spadek z 1023 przypisano poprawkom VP37; późniejszy DR-014 wyłączył z analizy
10 znalezisk rejestru tokenów innych modułów. Notatka nie podaje osobnego
końcowego wyniku po tej czynności. Pozostały kod ECU nadal podlegał analizie
bez bramki zerowej liczby znalezisk.

Na commicie `8cc5d2f` ten sam skan dał 1046. Współdzielona sesja
`sc_config_session_t` w `sc_command_handlers.c`, używana przez moduły firmware,
usunęła po jednym znalezisku 15.5 i 8.9 z `config.c`. Pozostawiła sześć
Advisory 8.7 dla API poleceń SC wywoływanego bezpośrednio przez testy hostowe;
zapisano je w DR-006, podobnie jak DR-005. Porównanie plików i reguł bez numerów
wierszy nie wykazało dodanych znalezisk.

Wynik 939 z 2026-09-22 na `a5560a4` nie był odświeżany pomiędzy `a5560a4`
i `8cc5d2f`. Różnica do 1046 obejmuje zmiany, których nie skanowano osobno.
Bez licencjonowanych tekstów reguł był to materiał do kontroli regresji,
nie deklaracja zgodności.

### Wcześniejszy punkt porównania

Skan z 2026-09-12 używał cppcheck 2.13.0, poprawionej konfiguracji projektu
i modelu atomików HAL, bez licencjonowanych tekstów reguł:

- **949** aktywnych znalezisk dla **33** reguł;
- 719 w `src/ECU`, 230 w `src/common`;
- bez podziału Mandatory / Required / Advisory;
- wynik do przeglądu, bez deklaracji zaliczenia lub certyfikacji.

Największe grupy: `misra-c2012-15.5` 323, `12.1` 168, `10.4` 86, `2.5` 81,
`17.7` 37. Pliki: `sc_command_handlers.c` 104, `obd_ford_diag.c` 92,
`dtcManager.c` 77, `sensors.c` 72 i `vp37.c` 72. Refaktor OBD zmniejszył
znaleziska bezpośrednio dotkniętych źródeł i nagłówków z 486 do 143 bez nowych wyciszeń.

### Poprawka konfiguracji skanu

Buildsystem odczytywał `HAL_ENABLE_*` z `hal_project_config.h` do flag `-D`,
a nagłówki HAL dołączały konfigurację przez `__has_include`. cppcheck nie
rozwiązywał żadnej z tych ścieżek. Wcześniejsze skany analizowały ECU
z wyłączonymi funkcjami opt-in: ukryte deklaracje wyglądały jak niejawne
wywołania (17.3), stałe CAN i routera były nierozpoznane (`misra-config`),
a kontrola układu EEPROM w `dtcManager.c` przerywała analizę tego pliku,
dając pozorne zero znalezisk.

Runner zaczął wymuszać konfigurację w każdej jednostce translacji, wybierać
gałąź GNU w `hal_compiler.h` i ładować model atomików kompilatora z HAL.
Model usunął dawne 17.3 i `misra-config` z `hal_mutex_once.h`. Pozostałe 17.3
w `sc_command_handlers.c` wynikało z makra domykania funkcji, którego analizator
nie rozwiązywał w każdej badanej konfiguracji.

Bezpośrednio wcześniejszy, porównywalny skan przed refaktorem OBD zawierał
1290 znalezisk dla 33 reguł. Starsze wyniki 1262/33 (2026-09-09),
1026/33 (2026-07-10) i 787/25 (2026-04-21) dotyczyły ograniczonej konfiguracji
i nie są bezpośrednio porównywalne. Do nowych porównań zachowuj `summary.txt`
i `rule-counts.txt`.

Runner ECU, rejestr odstępstw i ręczny workflow `.github/workflows/ecu-misra.yml`
pozostają punktami wejścia do analizy.

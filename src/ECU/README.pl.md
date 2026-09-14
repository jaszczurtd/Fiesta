# ECU

Moduł ECU elektroniki Forda Fiesty, obejmujący funkcje istotne dla bezpieczeństwa.

## VP37 i Adjustometer

Pętla nastawnika dawki VP37 odczytuje wersjonowany pomiar z osobnego modułu
RP2040 Adjustometer pod adresem I2C `0x57`. Szybki blok zawiera pozycję,
częstotliwość surową i filtrowaną, numer, czas i wiek próbki, napięcie,
temperaturę oraz status. Regulator odrzuca nieważny i stary pomiar.
Pierwotne rejestry pozostają dostępne dla starszych odbiorców; obecne ECU
wymaga Adjustometera obsługującego szybki blok.

ECU traktuje temperaturę paliwa jako przybliżenie temperatury cewki VP37
i skaluje całe sterowanie FF+PID względem punktu odniesienia 49°C. Ograniczony
współczynnik jest filtrowany po inicjalizacji; błędna temperatura zatrzymuje
jego ostatnią wartość. Fizyczne limity wyjścia są uwzględniane przed całkowaniem
PID. Model nie koryguje częstotliwości oscylatora ani baseline czujnika.
Błąd komunikacji wstrzymuje PID i po 20 ms wyłącza napęd;
nieważny status pozycji wyłącza go od razu.

Kompensacja napięcia korzysta z szybkiego lokalnego ADC ECU. Pierwsza poprawna
para po starcie lub błędzie lokalnego odczytu ustala skalę według Adjustometera;
pozostałe zmiany skali zachodzą tylko wtedy, gdy zadanie 0% doszło do MIN, czyli
warunku zwolnienia napędu. Adjustometer jest też źródłem zapasowym po błędzie
lokalnego ADC. Nieważne napięcie z obu źródeł wybiera
15 V, aby nie zwiększać sterowania. Histereza sample-and-hold 0,5 V tłumi
tętnienia powiązane z obciążeniem. Po przekroczeniu tego pasma bezpośrednia
korekcja `12 / Vc` przejmuje lokalny pomiar w bieżącym kroku regulatora.

Pętla regulatora używa jawnego okresu i rzeczywistego upływu sekund. Logi
zawierają osobne człony P/I/D oraz dostępne limity korekcji; wysyłanie odbywa
się poza mutexem regulatora. Tor pomiarowy opisuje
[README Adjustometera](../Adjustometer/README.pl.md) i wspólna
[mapa rejestrów I2C](../common/adjustometer_protocol.h).
Człon D jest domyślnie wyłączony, ponieważ opóźniony pomiar pozycji po udarze
mechanicznym może wzbudzić trwałą oscylację. Przy ustalonym celu całka jest
zatrzymywana w paśmie 20 Hz i wznawiana dopiero wtedy, gdy błąd przez 500 ms
pozostaje poza pasmem 40 Hz. Człon P działa przez cały czas.
Śledzenie zmian napięcia również zatrzymuje całkowanie.

Logi zapisują napięcie z Adjustometera po dolnym ograniczeniu jako `V`, lokalny
odczyt ADC jako `Vl`, wybrane wejście przed histerezą jako `Ve`, a napięcie
rzeczywiście użyte jako `Vc`. `vcor` jest zastosowanym mnożnikiem, `vt` oznacza
śledzenie zmian napięcia, a `ih` zatrzymanie całki dla ustalonego celu.

W kompilacji stanowiskowej można nadpisać `VP37_PWM_FREQUENCY_HZ` oraz cztery
wartości `CYCLIC_DELAYTIME_*`. Domyślne wartości to 200 Hz i kroki cyclic
4, 6, 12 oraz 2 ms, po sześć pełnych przebiegów 0-100-0. Krok 2 ms jest próbą
przeciążeniową, która przekracza zwykły limit rampy zadania.
`START_TEST_VP37_MODE=1` wybiera test cyclic, który zaczyna od zera i jest
uruchamiany poleceniem `C`. Limity czasu dodatniego zadania
obejmują rampę, a aktywny krok jest zapisany jako `cyms`.

`START_TEST_VP37_MODE=3` wybiera stałe zadanie z portu szeregowego. Polecenie
`S<0..100>` działa wtedy do następnego `S`, `X` albo restartu, a zapis RAM jest
dostępny bez uruchamiania cyclic. Nagłówek wybiera obecnie domyślnie tryb 3 dla
firmware stanowiskowego; build do pracy z silnikiem musi nadpisać go trybem 0.
W trybie 2 pojedyncza zmiana potencjometru o jeden procent musi utrzymać się
przez 150 ms; większe zmiany są przyjmowane od razu.

## Trwałe dane i GPS

ECU rezerwuje 32 KiB EEPROM emulowanego we flash. Obszar KV zaczyna się od
bajtu 4096 i zawiera dwa banki po 8192 bajty; pierwszy sektor jest wydzielony
poza KV. Firmware i testy hostowe używają układu z `hal_project_config.h`.
Stare banki KV pod adresami 96/128 są pomijane, bez migracji ich danych.
Kasowanie DTC usuwa wyłącznie klucze DTC i zachowuje zapisaną konfigurację.

Operacje parametrów i DTC są szeregowane na core 0. Callbacki zapisu EEPROM
zatrzymują GPS tylko na czas fizycznego zapisu flash i wznawiają go po próbie,
również po błędzie zapisu. Zwalnia to odbiornik PIO/DMA zgodnie z wymaganiami
koordynatora flash RP. Odczyty, niepoprawny układ pamięci, niezmienione wartości
i przygotowywanie danych w RAM pozostawiają GPS uruchomiony. Nieudane wznowienie
GPS jest logowane i ponawiane co sekundę; nie zmienia udanego zapisu w błąd.

## Migracja MISRA-C

ECU jest objęte migracją MISRA-C. Stan migracji, zabezpieczenia, zasady i sposób
uruchamiania analizy opisuje [`MISRA.md`](../../MISRA.md).

Narzędzia modułu:

- skrypt [`misra/check_misra.sh`](misra/check_misra.sh),
- wyciszenia i rejestr odstępstw w [`misra/`](misra/),
- ręczny workflow CI `.github/workflows/ecu-misra.yml`.

Zapisany wynik analizy z 2026-09-12 (cppcheck 2.13.0, bez licencjonowanych
tekstów reguł) obejmuje 949 zgłoszeń z 33 reguł. Służy do oceny i porządkowania
problemów; nie potwierdza zgodności. Szczegóły i ograniczenia porównania są
w [`MISRA.md`](../../MISRA.md).

## Budowanie

Firmware native:

```bash
cd src/ECU
JH=../../../libraries/JaszczurHAL/vscode/entry/jh-vscode
"$JH" build --project "$PWD"
"$JH" build-debug --project "$PWD"
"$JH" upload --project "$PWD"
"$JH" upload-uf2 --project "$PWD"
"$JH" refresh-intellisense --project "$PWD"
```

`upload` odpowiada zadaniu VS Code i skrótowi `Ctrl+Shift+2`; `upload-uf2`
korzysta z dysku BOOTSEL. `refresh-intellisense` odtwarza `compile_commands.json`,
`compile_commands_patched.json` i `.vscode/c_cpp_properties.json`. Polecenie
`monitor` odpowiada `Ctrl+Shift+3`. Skrót `Ctrl+Shift+9` ustawia
`jaszczurhal.uploadPort`; upload nadal sprawdza tożsamość urządzenia przez
`/dev/serial/by-id`. Walidacja manifestów Fiesty znajduje się w
`src/common/scripts/`; lokalne wrappery modułów zostały usunięte.

Testy hostowe, uruchamiane z katalogu głównego Fiesty:

```bash
cmake -S src/ECU -B src/ECU/build_test -DCMAKE_BUILD_TYPE=Release
cmake --build src/ECU/build_test --parallel
ctest --test-dir src/ECU/build_test --output-on-failure
```

Analiza MISRA:

```bash
cd src/ECU
bash misra/check_misra.sh --out misra/.results
```

Repozytorium nie zawiera licencjonowanych tekstów reguł MISRA Appendix A.
Lokalny plik można wskazać przez `--rule-texts /absolute/path/to/file`, aby
uzyskać pełniejsze komunikaty i podział według kategorii reguł.

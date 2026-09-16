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
Dla zamontowanego bocznika 0,22 Ω mapa podtrzymania ma mnożnik 1,08
(`VP37_PWM_FF_HARDWARE_GAIN`). Zwiększa on sterowanie bazowe przed kompensacją
napięcia i temperatury; człon ruchu i nastawy PID zachowują własne wartości.
Zakres kalibracji pozycji pozostaje bez zmian. GPIO26 jest zarezerwowane dla
wejścia bocznika; zapis na karcie SD wymaga innego pinu chip-select.
Błąd komunikacji wstrzymuje PID i po 20 ms wyłącza napęd;
nieważny status pozycji wyłącza go od razu.

Pomiar prądu na GPIO26 to skan taktowany sprzętowo (`HAL_ENABLE_ADC_SCAN`):
bocznik, multiplekser czujników (GPIO27) i dzielnik zasilania (GPIO28) są
przetwarzane naprzemiennie co 24 us, DMA wypełnia bloki o długości 2,25
okresu PWM (11,3 ms), a core 1 redukuje najnowszy blok raz na krok regulacji.
Bramkę odtwarza się z samego przebiegu bocznika (0,5 A włączenie, 0,25 A
wyłączenie, zbocze liczy się, gdy poziom utrzyma się trzy ramki): blok zawsze
zawiera jeden pełny okres od zbocza do zbocza niezależnie od fazy, a 60 us
przy obu zboczach fazy ON jest pomijane. `VP37 IPULSE` podaje średnią fazy ON
z ograniczeniem próbek do P95, P95, surowe maksimum, timing, kalibrację zera,
clipping, ważność, średnią zasilania ważoną fazami oraz liczbę bloków
zredukowanych i pominiętych. Są to pomiary bocznika w fazie ON; prąd freewheel
omija bocznik. Core 0 tylko drukuje raport. Podczas skanu odczyty na żądanie
tych trzech pinów zwracają najnowszą próbkę skanu, więc czytniki czujników nie
zmieniają się poza 60 us ustalania po zmianie kanału multipleksera. Komendy
stanowiskowe `Q0` i `Q1` wyłączają i włączają publikowanie obserwacji; raport
działa niezależnie. Przy 1 kHz i wyższych częstotliwościach okres konwersji
spada do 2 us na pin; nadal obowiązuje odrzucanie 60 us przy zboczach i minimum
osiem próbek, a krótka faza ON może dać za mało próbek - taki wynik pozostaje
nieważny.

Kompensacja napięcia korzysta z szybkiego lokalnego ADC ECU. Pierwsza poprawna
para po starcie lub błędzie lokalnego odczytu ustala skalę według Adjustometera;
pozostałe zmiany skali zachodzą tylko wtedy, gdy zadanie 0% doszło do MIN, czyli
warunku zwolnienia napędu. Adjustometer jest też źródłem zapasowym po błędzie
lokalnego ADC. Nieważne napięcie z obu źródeł wybiera
15 V, aby nie zwiększać sterowania. Odczyt lokalny powyżej skalibrowanego
zakresu 17 V nie jest błędem: dzielnik nasyca się przy ok. 18,8 V, więc taki
odczyt jest dolnym oszacowaniem szyny i dalej zmniejsza komendę, nie ucząc
skali (`vhi` w trace). Komenda podąża za szyną przez jeden krótki
filtr (`VP37_VOLTAGE_FILTER_S`, 50 ms) i nic więcej: bez pasma martwego i bez
okna śledzenia. Wejściem podstawowym jest napięcie uśrednione przez zadanie
pomiaru prądu po pełnym okresie PWM, ze średnimi faz ON i OFF ważonymi ich
czasem trwania. Nieważny wynik, wiek 100 ms, wyłączony pomiar albo zwolniony
napęd wybiera zwykły lokalny odczyt ADC. Stanowiskowe `V0` wybiera tor lokalny
wprost; `V1` jest ustawieniem domyślnym. Zmiana zasilania nigdy nie zatrzymuje
całkowania: jest zdejmowana z komendy, zanim komenda trafi do nastawnika.

Pętla regulatora używa jawnego okresu i rzeczywistego upływu sekund. Logi
zawierają osobne człony P/I/D oraz dostępne limity korekcji; wysyłanie odbywa
się poza mutexem regulatora. Tor pomiarowy opisuje
[README Adjustometera](../Adjustometer/README.pl.md) i wspólna
[mapa rejestrów I2C](../common/adjustometer_protocol.h).
Domyślne nastawy to P=0,05, I=0,20 i D=0 z okresem regulatora 5 ms.
Zatrzymanie całki wymaga ciągłego utrzymania błędu
wewnątrz 20 Hz przez 100 ms; krótkie przejście przez cel nie zamraża I.
Całkowanie wznawia się po 500 ms ciągłego błędu poza 40 Hz. Stanowiskowe `E<0..1000>` wybiera czas
potwierdzenia w ms; `E0` umożliwia porównanie z natychmiastowym zatrzymaniem.
`R` przywraca domyślne nastawy PID i potwierdzenia.

Logi zapisują napięcie z Adjustometera po dolnym ograniczeniu jako `V`, lokalny
odczyt ADC jako `Vl`, wybrane wejście przed filtrem jako `Ve`, a napięcie
rzeczywiście użyte jako `Vc`. `vcor` jest zastosowanym mnożnikiem, `ih` zatrzymanie całki dla ustalonego celu, a `vp`
użycie średniej napięcia z pełnego okresu. `VP37 CFG` zawiera `pwm_hz`.

W kompilacji stanowiskowej można nadpisać `VP37_PWM_FREQUENCY_HZ` oraz cztery
wartości `CYCLIC_DELAYTIME_*`. Domyślne wartości to 200 Hz i kroki cyclic
4, 6, 12 oraz 2 ms, po sześć pełnych przebiegów 0-100-0. Krok 2 ms jest próbą
przeciążeniową, która przekracza zwykły limit rampy zadania.
`START_TEST_VP37_MODE=1` wybiera test cyclic, który zaczyna od zera i jest
uruchamiany poleceniem `C`. Limity czasu dodatniego zadania
obejmują rampę, a aktywny krok jest zapisany jako `cyms`.

`START_TEST_VP37_MODE=3` wybiera stałe zadanie z portu szeregowego. Polecenie
`S<0..100>` działa wtedy do następnego `S`, `X` albo restartu, a zapis RAM jest
dostępny bez uruchamiania cyclic. Nagłówek wybiera obecnie domyślnie tryb 2 dla
stanowiska z potencjometrem; build do pracy z silnikiem musi nadpisać go trybem 0.
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

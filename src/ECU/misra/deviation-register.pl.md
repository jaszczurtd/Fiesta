# Rejestr odstępstw MISRA

[English](deviation-register.md) · [Zakres i zasady](../../../MISRA.pl.md)

Rejestr opisuje przyjęte odstępstwa, fałszywe alarmy i odłożone znaleziska
ECU, współdzielonego VP37 i Adjustometera. Zachowuje historię decyzji;
otwarty wpis nie jest wynikiem nowego skanu.

## Zasady

Każde aktywne wyciszenie musi mieć wpis. Preferuj poprawkę kodu; zamknięcie
Required lub Mandatory wymaga uzasadnienia i jawnej decyzji.
Usunięte odstępstwa zachowuj ze statusem `closed`.

`tool-fp` oznacza potwierdzony fałszywy alarm, `tool-context` ograniczenie
konfiguracji skanu, `accepted-deviation` przyjęty wyjątek, `pending-fix`
pracę oczekującą na rozstrzygnięcie, a `bootstrap` założenie rejestru.
Status to `open`, `planned`, `closed` lub `none`. `n/a` oznacza brak reguły.

Początek rejestru opiera się na skanie z 2026-09-12 z konfiguracją projektu
i modelem atomików HAL. Późniejsze wpisy podają własne daty decyzji.
Znaleziska ze starszych, ograniczonych konfiguracji nie zostały przeniesione.

## Indeks

| ID | Reguła | Typ | Status |
|---|---|---|---|
| [DR-000](#dr-000) | n/a | bootstrap | closed |
| [DR-001](#dr-001) | 17.3 | tool-fp | open |
| [DR-002](#dr-002) | 17.3 | tool-fp | closed |
| [DR-003](#dr-003) | 21.6, 11.3, 10.4, 10.8, 15.7 | pending-fix | planned |
| [DR-004](#dr-004) | 17.7 | pending-fix | planned |
| [DR-005](#dr-005) | 8.7 | accepted-deviation | open |
| [DR-006](#dr-006) | 8.7 | accepted-deviation | open |
| [DR-007](#dr-007) | 19.2 | accepted-deviation | open |
| [DR-008](#dr-008) | 21.25 (Amendment 4) | accepted-deviation | open |
| [DR-009](#dr-009) | 15.5 | accepted-deviation | open |
| [DR-010](#dr-010) | 8.7 | tool-fp | open |
| [DR-011](#dr-011) | 2.5 | accepted-deviation | open |
| [DR-012](#dr-012) | 2.5 | tool-context | open |
| [DR-013](#dr-013) | 2.5 | tool-context | open |
| [DR-014](#dr-014) | 2.5 | tool-context | open |

## DR-000

Reguła: **n/a**. Typ: `bootstrap`. Status: `closed`.

Zakres: `src/ECU/misra/*`.

Wyciszenie: brak.

Założenie rejestru. Zastąpione wpisami poniżej.

## DR-001

Reguła: **17.3**. Typ: `tool-fp`. Status: `open`.

Zakres: `src/common/scDefinitions/sc_command_handlers.c`: użycie `HAL_COMMAND_SOURCE_MASK`.

Wyciszenie: brak.

`HAL_COMMAND_SOURCE_MASK` jest w `hal_command_router.h` za `HAL_ENABLE_COMMAND_ROUTER`,
wyprowadzanym przez HAL z `HAL_ENABLE_SERIAL_COMMANDS`. cppcheck nie rozwiązuje tej
zależności w każdej analizowanej konfiguracji, więc użycie makra wygląda jak niejawne
wywołanie funkcji. Kompilacja firmware przyjmuje tę samą linię bez diagnostyki.

## DR-002

Reguła: **17.3**. Typ: `tool-fp`. Status: `closed`.

Zakres: JaszczurHAL `src/hal/core/hal_mutex_once.h`.

Wyciszenie: brak.

HAL kieruje atomiki kompilatora przez `hal_compiler.h`, a model cppcheck obejmuje
odczyty atomowe i warunki compare-exchange. Dawne artefakty 17.3 i `misra-config` już
nie występują.

## DR-003

Reguła: **21.6, 11.3, 10.4, 10.8, 15.7**. Typ: `pending-fix`. Status: `planned`.

Zakres: `src/common/scDefinitions/*.c`.

Wyciszenie: brak.

Dwanaście znalezisk Required w źródłach kompilowanych do ECU, Clocks i OilAndSpeed:
`<stdio.h>` dla `snprintf`, dwa rzutowania wskaźników w dostępie do pól przez offset
deskryptora oraz poprawki typów i `else`. Decyzja czeka na rozstrzygnięcie, czy
`src/common/scDefinitions` podlega wymaganiom MISRA.

## DR-004

Reguła: **17.7**. Typ: `pending-fix`. Status: `planned`.

Zakres: `src/common/scDefinitions/sc_param_handlers.c`.

Wyciszenie: brak.

Osiem ignorowanych wyników. Wyniki `snprintf` mogą ukrywać obcięcie odpowiedzi i
wymagają poprawki; `memcpy` zwraca wskaźnik docelowy, więc jego ignorowanie nie ukrywa
błędu operacji. Zakres oczekuje na tę samą decyzję co DR-003.

## DR-005

Reguła: **8.7**. Typ: `accepted-deviation`. Status: `open`.

Zakres: `src/ECU/engineMaps.c` oraz `src/common/vp37`: `vp37.c`, `vp37_feedback.c`, `vp37_compensation.c`, `vp37_control.c`, `vp37_telemetry.c`, `vp37_current.c`.

Wyciszenie: `misra-c2012-8.7:*vp37/vp37.c`, `misra-c2012-8.7:*vp37/vp37_current.c`.

Advisory. Mapy silnika mają zewnętrzne wiązanie i po jednym module odbierającym, aby
każda tabela miała jedno miejsce edycji. VP37 jest podzielony na tematyczne jednostki
translacji, a funkcje kroku mają pojedynczych odbiorców w innych jednostkach;
przeniesienie ich do plików wywołujących zniosłoby ten podział. Skan obejmuje tylko
`src/ECU/*.c` i `src/common/vp37/*.c`, więc funkcje używane też przez testy hostowe
wyglądają jak używane z jednej jednostki. Ten checker w ogóle nie zgłasza symbolu, który
plik definiuje bez lokalnego użycia, jak mapy i funkcje wydzielonych kroków. Dlatego
wyciszenia są tylko w dwóch jednostkach nadal wywołujących własne funkcje zewnętrzne.
Decyzja: 2026-09-16.

## DR-006

Reguła: **8.7**. Typ: `accepted-deviation`. Status: `open`.

Zakres: `src/common/scDefinitions/sc_command_handlers.c`: `sc_command_service_init`, `sc_command_service_router`, `sc_command_format_serial_response`, `sc_command_allow_inactive_reboot`, `sc_command_reply_legacy_unknown`, `sc_command_service_process_deferred`.

Wyciszenie: `misra-c2012-8.7:*sc_command_handlers.c`.

Advisory. Moduły uruchamiają sesję przez `sc_config_session_t` w tej samej jednostce.
Skan nie obejmuje testów hostowych, więc widzi wskazane funkcje jako używane z jednej
jednostki. Zachowują wiązanie zewnętrzne, ponieważ `tests/test_sc_command_handlers.cpp`
sprawdza usługę bezpośrednio z własnymi routerami i niepoprawnymi konfiguracjami.
Wiązanie lokalne odebrałoby tę możliwość. To sytuacja analogiczna do DR-005. Wpis nie
rozstrzyga zakresu `src/common/scDefinitions` z DR-003/004; wycisza tylko 8.7 w tym
pliku. Decyzja: 2026-09-29.

## DR-007

Reguła: **19.2**. Typ: `accepted-deviation`. Status: `open`.

Zakres: `src/common/vp37/vp37_snapshot.c`: reprezentacja floatów publikowanego statusu i widok słów bufora roboczego telemetrii.

Wyciszenie: `misra-c2012-19.2:*vp37_snapshot.c`.

Advisory. Rdzeń sterujący współdzieli stan wyłącznie jako słowa `uint32_t` odczytywane i
zapisywane atomowo, usuwając wyścig zwykłej kopii; porządki pamięci opisuje DR-008.

Floaty i struktura muszą zostać zamienione na słowa i z powrotem. C oferuje `memcpy`,
naruszające Required 21.15 dla niezgodnych typów wskaźników, albo unię, której odczyt
członu interpretuje zapisaną reprezentację według C11 6.5.2.3. `_Static_assert` obok
konwersji wymaga jednakowego rozmiaru `float` i `uint32_t`. Słowa statusu nie zawierają
paddingu, ponieważ powstają z czterech pól, nie z całej struktury.

Słowa telemetrii zawierają padding `VP37Telemetry`, którego zawartość nie jest używana.
Unia robocza ma statyczny czas życia: przed pierwszą publikacją każdy bajt jest zerowy
(C11 6.7.9p10). Po zapisach członów padding ma wartości nieokreślone w danym zapisie (unspecified, 6.2.6.1p6), zaliczane przez C11 do indeterminate (3.19.2).
Odczyt jako `uint32_t`, typ o dokładnej szerokości bez paddingu i reprezentacji
pułapkowej (7.20.1.1), daje poprawną wartość i nie jest niezdefiniowanym zachowaniem.
Odbiorca wpisuje te bajty tylko do paddingu własnej `VP37Telemetry`; kod odczytuje
nazwane pola. Unie są lokalne dla jednostki. Testy hostowe sprawdzają dokładne
odtworzenie wszystkich wartości, także z ThreadSanitizer. Decyzja: 2026-09-30.

## DR-008

Reguła: **21.25 (Amendment 4)**. Typ: `accepted-deviation`. Status: `open`.

Zakres: `HAL_ATOMIC_*` z relaxed, acquire, release lub acquire-release w `src/common/vp37/vp37_snapshot.c` (publikacja), `vp37_telemetry.c` (bufor śladu) i `src/ECU/tests_helpers.c` (kolejka konsoli, żądanie konfiguratora, czas kontaktu, flaga sesji i parametry testów).

Wyciszenie: brak.

Reguła wymaga sequential consistency dla funkcji synchronizacji `<stdatomic.h>`. Te
jednostki używają wbudowanych GCC `__atomic` przez HAL, lecz to samo zagadnienie
uzasadnia zapisanie decyzji.

Kopia stanu używa licznika sekwencji z jednym zapisującym, według Boehma „Can seqlocks
get along with programming language memory models?" (2012). Zapisujący ustawia
nieparzysty licznik, wykonuje release fence, zapisuje słowa relaxed i publikuje parzysty
licznik z release. Czytający ładuje licznik z acquire, słowa relaxed, wykonuje acquire
fence i ponownie ładuje licznik. Kopia jest przyjmowana tylko dla dwóch identycznych
parzystych odczytów licznika, więc obejmuje całą publikację.

Sequential consistency słów dodałoby bariery przy każdym z około 310 zapisów w kroku 5
ms na Cortex-M0+, który nie ma instrukcji store-release, bez zmiany wyniku. Bufor śladu
i warstwa testów przekazują dane między rdzeniami: zwykły dostęp następuje przed release
po stronie nadawcy i po acquire po stronie odbiorcy. Żaden z opisanych algorytmów nie
zależy od porządku zapisu i późniejszego odczytu innej zmiennej, dla którego sequential
consistency wnosi dodatkowe gwarancje.

Dowody zapisane przy decyzji: testy kopii stanu z ThreadSanitizer i przeplataniem
publikacji; zastąpienie słów atomowych zwykłą kopią zgłasza wyścig. Testy działają na
x86 z silniejszym porządkiem sprzętowym, więc uzasadnienie RP2040 opiera się na modelu
C11 i barierach `dmb` emitowanych przez kompilator. cppcheck 2.13 nie sprawdza Amendment
4; wpis dokumentuje decyzję. Data: 2026-09-30.

## DR-009

Reguła: **15.5**. Typ: `accepted-deviation`. Status: `open`.

Zakres: `src/common/vp37/*.c` i `src/Adjustometer/*.c`.

Wyciszenie: `misra-c2012-15.5:*vp37/*.c`, Adjustometer: `misra-c2012-15.5` we własnych wyciszeniach.

Advisory, pojedynczy punkt wyjścia. Oba moduły stosują warunki odrzucenia na początku
funkcji: sprawdzają przyczynę, raportują ją i wracają, pozostawiając dalszy kod bez
dodatkowego wcięcia. Sprowadzenie ponad 60 funkcji do jednego wyjścia wymagałoby
zagnieżdżeń albo flag wyniku w pętli sterowania tuż przed bramką sprzętową, bez zmiany
zachowania. Współdzielone nagłówki inline `adjustometer_feedback.h` i
`fiesta_sensor_helpers.h` stosują ten sam styl. Użytkownik wybrał go 2026-09-30 podczas
poprawiania innych reguł Advisory.

## DR-010

Reguła: **8.7**. Typ: `tool-fp`. Status: `open`.

Zakres: `src/common/vp37/vp37_snapshot.c`: `VP37_snapshotBegin`, `VP37_snapshotEnd`.

Wyciszenie: `misra-c2012-8.7:*vp37/vp37_snapshot.c`.

Skan definiuje `UNIT_TEST`, więc `TESTABLE_STATIC` rozwija się do pustego tekstu, aby
testy hostowe mogły deterministycznie wywołać odczyt przerwany publikacją. Firmware
kompiluje obie funkcje jako static. Znalezisko dotyczy konfiguracji skanu. To ten sam
mechanizm co symbole `TESTABLE_STATIC` w `sensors.c`. Decyzja: 2026-09-30.

## DR-011

Reguła: **2.5**. Typ: `accepted-deviation`. Status: `open`.

Zakres: `src/common/adjustometer_protocol.h`.

Wyciszenie: `misra-c2012-2.5:*adjustometer_protocol.h` w obu skanach.

Advisory, nieużywane makro. Nagłówek definiuje mapę rejestrów protokołu
ECU-Adjustometer; każda strona używa tylko części, więc skan widzi pozostałe rejestry,
flagi i liczniki jako nieużywane. Mapa celowo pozostaje pełna:
`ADJUSTOMETER_REG_PULSE_LO` dokumentuje rejestr 0x01, choć czytający adresuje blok od
pierwszego rejestru. Decyzja: 2026-09-30.

## DR-012

Reguła: **2.5**. Typ: `tool-context`. Status: `open`.

Zakres: `src/common/vp37/vp37_config.h` i `vp37_current.h`: interwały telemetrii, okres odpytywania i długość śladu.

Wyciszenie: `misra-c2012-2.5:*vp37/*.h` w obu skanach, inline `misra-c2012-2.5`.

Advisory, nieużywane makro. Nagłówki definiują makra dla obrazu stanowiskowego, podczas
gdy skan analizuje obraz samochodu bez tej telemetrii; dla innych jednostek (adapter ECU
czyta piny stopnia mocy); oraz dla obu gałęzi `#if` na `VP37_PWM_FREQUENCY_HZ`. cppcheck
raportuje nieużywane makra dla badanej konfiguracji, a linia zależy od grafu include.
Wyciszenie obejmuje więc regułę 2.5 w nagłówkach modułu jako plikach; znane definicje
stanowiskowe mają też wyciszenia inline. Kontrola martwych makr nadal obejmuje pliki
`.c`. Decyzja: 2026-09-30.

## DR-013

Reguła: **2.5**. Typ: `tool-context`. Status: `open`.

Zakres: `hal_project_config.h` modułów: makra odczytywane przez buildsystem HAL.

Wyciszenie: inline `cppcheck-suppress misra-c2012-2.5 ; DR-013`.

Advisory, nieużywane makro. `HAL_ENABLE_*`, rozmiary stosów i wybór trybu wejścia
czytają `hal_config.h` oraz CMake, a nie własny kod C modułu. Dlatego skan modułu widzi
je jako nieużywane. Wyciszenia inline są w osiąganych przez skan
`src/Adjustometer/hal_project_config.h` i `src/VP37TestBench/hal_project_config.h`.
Pojawiły się ze skanami kroku 4; wpis dodano 2026-09-30 po wykryciu brakującego
uzasadnienia przez audyt.

## DR-014

Reguła: **2.5**. Typ: `tool-context`. Status: `open`.

Zakres: `src/common/scDefinitions/sc_fiesta_module_tokens.h`.

Wyciszenie: `misra-c2012-2.5:*sc_fiesta_module_tokens.h` w obu skanach.

Advisory, nieużywane makro. Generowany rejestr definiuje `SC_MODULE_TOKEN_*` dla każdego
modułu z `modules.json`, a firmware używa tylko własnego. Każdy skan widzi więc
pozostałe tokeny jako nieużywane. Rejestr zachowuje kompletność, ponieważ
`fiesta_modules.py --check` porównuje go z listą modułów. Bramka 2026-09-30 wykryła
wzrost o 1 po dodaniu `VP37TB`; wyciszenie pliku obowiązuje w obu skanach.

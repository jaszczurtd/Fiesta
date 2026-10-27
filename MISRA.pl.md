# Zakres MISRA-C i zasady przeglądu

[English](MISRA.md) · [O projekcie](README.pl.md) · [Kontrole hostowe](doc/TESTING.pl.md)

Fiesta używa dodatku MISRA do cppcheck, aby wykrywać i analizować naruszenia
reguł. Projekt nie deklaruje formalnej zgodności MISRA ani procentowego postępu
migracji. Czysty wynik dotyczy tylko sprawdzonej konfiguracji i reguł
obsługiwanych przez analizator.

## Zakres i kryteria

| Kod | Sposób sprawdzania |
|---|---|
| `src/common/vp37`, Adjustometer i wybrane nagłówki współdzielone | Bramka 4 w `runalltests.sh` odrzuca aktywne znaleziska po uwzględnieniu zapisanych wyciszeń. |
| Pozostały kod ECU i źródła współdzielone włączone przez runner | Raport do przeglądu; brak wymogu zerowej liczby znalezisk w bramce. |
| Clocks, Fiesta_clock i własne pliki VP37TestBench | Poza zakresem MISRA. Współdzielony VP37 używany przez stanowisko pozostaje w zakresie. |

Bramka uruchamia `check_misra.sh --fail-paths` dla ECU i drugi skan
z `--project src/Adjustometer`. Wyrażenia ścieżek w
[`runalltests.sh`](runalltests.sh) wybierają VP37 oraz związane z nim pliki
`adjustometer_*`, `fiesta_*` i `vp37_drive_config`. Wyjątki opisuje
[rejestr odstępstw](src/ECU/misra/deviation-register.pl.md).
Wymagany wynik to brak aktywnych znalezisk w tych ścieżkach. Dawny poprawny
przebieg nie potwierdza wyniku dla bieżącego checkoutu.

## Implementacja i pozostałe prace

Źródła ECU są kompilowane jako C z Pico SDK, ale końcowe linkowanie obejmuje
kod C++ HAL. Instancje sterowania znajdują się w `ecu_context_t`; CAN,
czujniki, DTC, czas RTC, OBD i sesja przechowują stan lokalnie w plikach. Publiczne
nagłówki ECU zapewniają wiązanie C, a PID i timery programowe mają interfejsy
C w HAL. Testy hostowe i firmware ECU używają `-Werror`.

VP37 sprawdza upływ czasu i status PID, a błędny krok wyłącza stopień wyjściowy.
Zmiany strojenia wykonuje rdzeń sterujący przez funkcje sprawdzające zakresy.
Rdzeń 0 odczytuje atomową kopię stanu do telemetrii. Kopie pomiarów
Adjustometera, stan wyjść PCF8574 i dostęp do DTC/KV mają jawną synchronizację.
Bufory nadawania CAN są inicjalizowane, odbiór sprawdza wskaźniki i długości,
a zapis ostatniego dostarczonego RPM zmienia się dopiero po udanym nadaniu.
Publikacja uwzględnia ponawianie i okresowe potwierdzanie obecności.

Pozostaje przegląd rzutowań, granic i przepełnień w diagnostyce Forda oraz
innych modułach ECU, nazewnictwa i użycia volatile/muteksów. Linkowanie wyłącznie
w C wymagałoby też zastąpienia pozostałych zależności C++ w HAL i testach.
Obecność interfejsów C nie oznacza linkowania całej aplikacji bez C++.

## Uruchamianie i porównywanie analizy

Z katalogu głównego repozytorium:

```bash
cd src/ECU
bash misra/check_misra.sh --out misra/.results
```

Użyj wersji cppcheck wybranej przez HAL. Zachowaj raport wraz z commitami
Fiesty i HAL, wersją narzędzia, wyborem projektu, flagami i plikami wyciszeń.
Porównuj `summary.txt`, `rule-counts.txt` i `active-findings.txt` tylko dla
równoważnych konfiguracji. [Instrukcja runnera](src/ECU/misra/README.pl.md)
opisuje opcje i pliki wynikowe, a [ecu-misra.yml](.github/workflows/ecu-misra.yml)
pozwala ręcznie uruchomić analizę w CI.

[Historyczne wyniki](src/ECU/misra/history.pl.md) są oddzielone od bieżących
zasad. To datowane obserwacje, a nie baza dla checkoutu bez nowego skanu.
Ten przegląd nie deklaruje jednej aktualnej liczby znalezisk. Nowa baza
porównawcza wymaga zachowanych wyników i metadanych sprawdzonej rewizji.

## Zasady przeglądu

Najpierw popraw kod, zanim rozważysz wyciszenie. Każde aktywne wyciszenie,
także fałszywego alarmu narzędzia, musi mieć uzasadniony wpis w rejestrze.
Utrzymuj zgodność rejestru, plików wyciszeń i opisu zakresu przy każdej zmianie
związanej z MISRA. Zachowuj zamknięte wpisy jako historię. Zamknięcie znalezisk
Required lub Mandatory wymaga jawnej decyzji i uzasadnienia.

Repozytorium nie zawiera licencjonowanych tekstów reguł MISRA. Lokalny wyciąg
z licencją umożliwia podział raportu na Mandatory / Required / Advisory.
Bez niego wynik pozostaje materiałem do przeglądu i nie stanowi certyfikatu zgodności.

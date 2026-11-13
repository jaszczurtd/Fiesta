# Stan projektu i dalsze kroki

[English](STATUS.md) · [Strona główna](../README.pl.md)

Fiesta jest działającym prototypem rozproszonego systemu sterowania samochodem.
Prace nad ECU i VP37 odbywają się na silniku zamontowanym na osobnej stalowej
ramie. Clocks i funkcje nadzorcze ECU były używane w samochodzie bez sterowania
VP37. Pełna instalacja z VP37 nie została zwalidowana w samochodzie drogowym.

## Co jest dostępne

| Obszar | Obecny zakres | Od czego zacząć |
| --- | --- | --- |
| ECU i VP37 | Regulacja położenia, kalibracja, diagnostyka i pomiary stanowiskowe. Opcjonalny nadzorca rozruchu i biegu jałowego jest domyślnie wyłączony; żądanie kierowcy trafia bezpośrednio do regulatora położenia. | [Uruchomienie ECU](../src/ECU/README.pl.md) |
| Pomiary bieżące i zapisane | Build i upload ECU BENCH, zapis portu, wykresy na żywo i porównania offline. | [Viewer](../tools/vp37-viewer/README.pl.md) |
| Zestaw wskaźników | Fabryczne zegary, odczyty TFT, ostrzeżenia i testy wyjść. | [Clocks](../src/Clocks/README.pl.md) |
| Czas i temperatura | RTC, synchronizacja czasu z GPS, publikacja CAN i lokalny wyświetlacz. | [Fiesta_clock](../src/Fiesta_clock/README.pl.md) |
| Pomiar położenia | Pomiar częstotliwości Adjustometera i sprzężenie przez I²C. | [Adjustometer](../src/Adjustometer/README.pl.md) |
| Konfiguracja | GUI i CLI dla Linuksa: wykrywanie, ustawienia i testy funkcjonalne; GUI wgrywa też firmware. | [SerialConfigurator](../src/SerialConfigurator/README.pl.md) |
| DPF | Nieaktywna pozycja configuratora; brak firmware, wykrywania i wgrywania. | [Architektura systemu](../ARCHITECTURE.pl.md) |

## Sprzęt i firmware

Obecność projektu PCB w repozytorium nie potwierdza zgodności z firmware.
Przed wgraniem sprawdź rewizję płytki oraz assembly procesora.

| Sprzęt | Związek z firmware | Pozostałe prace lub ograniczenie |
| --- | --- | --- |
| Obecne okablowanie ECU z Waveshare RP2350-Plus lub RP2040-Plus | Bieżące assembly `rp2350-plus` i `rp2040-plus`. Adjustometer i ekspander współdzielą I²C0. | PCF8574A pracuje z ustawionymi 400 kHz mimo znamionowych 100 kHz. Zobacz [interfejsy ECU](../src/ECU/guides/BENCH.pl.md#obraz-stanowiskowy-i-sprzęt). |
| [ECU PCB 3](../Fiesta_pcbs/ecu/README.pl.md) | Nowa płytka rozdziela magistrale Adjustometera i ekspandera oraz używa nieodwracającego TC4427. | Dostosowanie magistral i polaryzacji wyjść, potem walidacja na wykonanej płytce. Nie należy zakładać zgodności z obecnym firmware. |
| [Płytka Fiesta_clock](../Fiesta_pcbs/clock/README.pl.md) | Nowy projekt używa DS3231 Mini; firmware domyślnie wybiera PCF8563 i udostępnia wariant DS3231. | Wybór wariantu RTC oraz dostosowanie i sprawdzenie przeliczenia nowego dzielnika napięcia. |
| [Płytka Clocks](../Fiesta_pcbs/dashboard/Clocks/README.pl.md) | Sprzęt zestawu wskaźników i jego instrukcja montażu. | Zasilanie z USB albo samochodu, nigdy jednocześnie. |
| [Płytka Adjustometera](../Fiesta_pcbs/vp37_adjustometer/adjustometer/README.pl.md) | Czujnik położenia dla ECU i osobnego VP37TestBench. | Sprawdzenie obu zasilań, wspólnej masy i kolejności pomiaru bazowego. |
| Osobny VP37TestBench | Własny projekt, wyświetlacz i połączenie załączania stopnia mocy. | Korzystaj z [instrukcji modułu](../src/VP37TestBench/README.pl.md); nie zastępuje wariantu BENCH ECU. |

## Dalsze prace i ich zależności

Do zakończenia integracji pozostaje dopasowanie firmware do nowych płytek,
ich sprawdzenie elektryczne, a następnie walidacja całej instalacji sterowania
silnikiem. Strojenie regulatora wymaga pomiarów układu mechanicznego.
Udany build lub test hostowy nie zamyka tych prac. To zależności między
zadaniami, a nie harmonogram wydania ani deklaracja gotowości instalacji
samochodowej.

Do bieżącej pracy służą [instrukcja kompilacji](BUILDING.pl.md),
[testy hostowe](TESTING.pl.md) i przewodnik modułu odpowiadającego sprzętowi.
Aby poznać system, zacznij od [architektury](../ARCHITECTURE.pl.md),
a następnie przejdź do odpowiedniego modułu.

## Granice walidacji

Projekt nie deklaruje zgodności z ISO 26262, AUTOSAR ani formalnej zgodności
MISRA. [Analiza MISRA](../MISRA.pl.md) obejmuje ECU, współdzielony kod VP37
i Adjustometer. Testy hostowe używają mocków HAL i nie potwierdzają zachowania
elektrycznego ani mechanicznego. Jest stanowisko silnikowe, lecz nie ma
automatycznego stanowiska HIL. RP2040 i RP2350 nie mają kwalifikacji AEC-Q100.
